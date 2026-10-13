#!/usr/bin/env python3
"""Build and locate the pinned Rklings guest kernel."""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tarfile
import time
import tomllib
import urllib.error
import urllib.request


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from engine.console import Console  # noqa: E402


UI = Console("rklings.kernel")
RUNTIME = ROOT / ".rklings"
TOOLBOX = ROOT / "scripts/toolbox"
MANIFEST_PATH = ROOT / "vm/kernel.toml"
BUILD_RECIPE_REVISION = 1
MANIFEST_FIELDS = {
    "schema_version", "architecture", "version", "local_version",
    "compiler", "base_config", "source_base_url", "source_sha256",
    "source_size", "config",
}

MAX_ARCHIVE_MEMBERS = 200_000
MAX_UNPACKED_SIZE = 2 * 1024 * 1024 * 1024
MIN_BUILD_SPACE = 4 * 1024 * 1024 * 1024
REQUIRED_FINAL_CONFIG = {
    "CONFIG_ARCH_HAS_SYSCALL_WRAPPER": "y",
    "CONFIG_CC_IS_GCC": "y",
    "CONFIG_CRYPTO_HASH": "y",
    "CONFIG_CRYPTO_LIB_UTILS": "y",
    "CONFIG_KRETPROBE_ON_RETHOOK": "y",
    "CONFIG_OPTPROBES": "y",
}
REQUIRED_BUILD_FILES = (
    "arch/x86/boot/bzImage", "vmlinux", "System.map", "Module.symvers",
    ".config", "include/config/auto.conf", "include/generated/autoconf.h",
    "scripts/module.lds",
)
HASHED_BUILD_FILES = REQUIRED_BUILD_FILES
SDK_INPUT_PATHS = (
    "source/Makefile", "source/Kbuild", "source/include",
    "source/arch/x86/Makefile", "source/arch/x86/Makefile_32.cpu",
    "source/arch/x86/include", "source/scripts",
    "build/Makefile", "build/include", "build/arch/x86/include",
    "build/scripts", "build/tools/objtool",
)
_HEX64 = re.compile(r"[0-9a-f]{64}\Z")
_BUILD_TOOLS = (
    "bash", "make", "ld", "as", "ar", "nm",
    "objcopy", "objdump", "readelf", "bc", "bison", "flex", "perl", "gzip",
)


class KernelError(RuntimeError):
    pass


def fail(message: str) -> None:
    raise KernelError(message)


def interrupt_for_cleanup(_signum: int, _frame: object) -> None:
    raise KeyboardInterrupt


def _ensure_dir(path: Path, *, create: bool) -> None:
    if path.is_symlink():
        fail(f"managed path is a symlink: {path}")
    if create:
        path.mkdir(parents=True, exist_ok=True)
    if path.exists() and not path.is_dir():
        fail(f"managed path is not a directory: {path}")


def _search_path() -> str:
    return os.fspath(TOOLBOX) + os.pathsep + os.environ.get("PATH", os.defpath)


def regular_nonempty(path: Path) -> bool:
    try:
        return not path.is_symlink() and path.is_file() and path.stat().st_size > 0
    except OSError:
        return False


def validate_repository_path(path: Path) -> None:
    if not re.fullmatch(r"/[A-Za-z0-9._+/-]*", os.fspath(path)):
        fail(
            "the repository path may contain only ASCII letters, digits, "
            "'/', '.', '_', '+', and '-' (required by Kbuild)"
        )


def read_manifest() -> dict[str, object]:
    validate_repository_path(ROOT)
    try:
        data = tomllib.loads(MANIFEST_PATH.read_bytes().decode("utf-8"))
    except (OSError, UnicodeDecodeError, tomllib.TOMLDecodeError) as error:
        fail(f"cannot read {MANIFEST_PATH.relative_to(ROOT)}: {error}")
    if set(data) != MANIFEST_FIELDS:
        fail("vm/kernel.toml fields do not match schema 1")
    if type(data["schema_version"]) is not int or data["schema_version"] != 1:
        fail("vm/kernel.toml schema_version must be integer 1")
    for field in MANIFEST_FIELDS - {"schema_version", "source_size"}:
        value = data[field]
        if not isinstance(value, str) or not value or any(ord(c) < 32 for c in value):
            fail(f"vm/kernel.toml {field} must be a nonempty printable string")
    if data["architecture"] != "x86_64":
        fail("only the x86_64 course kernel is currently supported")
    if data["base_config"] != "allnoconfig":
        fail("vm/kernel.toml base_config must be allnoconfig")
    version = str(data["version"])
    if not all(p.isdigit() for p in version.split(".")) or len(version.split(".")) != 3:
        fail("vm/kernel.toml version must have three numeric components")
    if not re.fullmatch(r"-[A-Za-z0-9][A-Za-z0-9-]*", str(data["local_version"])):
        fail("vm/kernel.toml local_version must be a simple dash-prefixed suffix")
    checksum = str(data["source_sha256"])
    if not _HEX64.fullmatch(checksum):
        fail("vm/kernel.toml source_sha256 must be a lowercase SHA-256 digest")
    if type(data["source_size"]) is not int or not 1024 <= data["source_size"] <= 1024**3:
        fail("vm/kernel.toml source_size must be an integer between 1 KiB and 1 GiB")
    relative_config = PurePosixPath(str(data["config"]))
    if relative_config.is_absolute() or ".." in relative_config.parts:
        fail("vm/kernel.toml config must stay inside the repository")
    config = ROOT.joinpath(*relative_config.parts)
    if config.is_symlink() or not config.is_file():
        fail(f"kernel config is not a regular file: {config.relative_to(ROOT)}")
    return data


def release_name(manifest: dict[str, object]) -> str:
    return str(manifest["version"]) + str(manifest["local_version"])


def tool_identity(command: str) -> str:
    sp = _search_path()
    resolved = shutil.which(command, path=sp)
    if not resolved:
        return f"missing:{command}"
    result = subprocess.run(
        [resolved, "--version"], check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        env={"PATH": sp, "LC_ALL": "C", "LANG": "C"},
    )
    if result.returncode or not result.stdout.strip():
        fail(f"cannot identify build tool: {command}")
    first_line = next(l for l in result.stdout.splitlines() if l.strip())
    return f"{Path(resolved).resolve()}\n{first_line}"


def libelf_identity(compiler: str) -> str:
    result = subprocess.run(
        [compiler, "-print-file-name=libelf.so"], check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        env={"PATH": os.environ.get("PATH", os.defpath), "LC_ALL": "C", "LANG": "C"},
    )
    candidate = Path(result.stdout.strip())
    if result.returncode or not candidate.is_absolute() or not candidate.is_file():
        fail("cannot identify the libelf linker library")
    return f"{candidate.resolve()}\n{sha256_file(candidate)}"


def build_identity(manifest: dict[str, object]) -> str:
    digest = hashlib.sha256(b"rklings-kernel-build-v1\0")
    def _feed(data: bytes) -> None:
        digest.update(len(data).to_bytes(8, "big"))
        digest.update(data)
    _feed(str(BUILD_RECIPE_REVISION).encode("ascii"))
    for path in (MANIFEST_PATH, ROOT / str(manifest["config"]), TOOLBOX / "bc"):
        _feed(path.read_bytes())
    compiler = str(manifest["compiler"])
    for command in (compiler, *_BUILD_TOOLS):
        _feed(tool_identity(command).encode())
    _feed(libelf_identity(compiler).encode())
    return digest.hexdigest()


def layout(manifest: dict[str, object]) -> dict[str, Path | str]:
    release = release_name(manifest)
    identity = build_identity(manifest)
    kit = RUNTIME / "kernel/kits" / f"{release}-{identity[:16]}"
    filename = f"linux-{manifest['version']}.tar.xz"
    return {
        "release": release, "build_identity": identity, "kit": kit,
        "source": kit / "source", "build": kit / "build",
        "image": kit / "build/arch/x86/boot/bzImage",
        "vmlinux": kit / "build/vmlinux", "stamp": kit / "stamp.json",
        "archive": RUNTIME / "cache" / filename,
        "url": str(manifest["source_base_url"]).rstrip("/") + "/" + filename,
    }


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sdk_digest(kit: Path, inputs: tuple[str, ...] = SDK_INPUT_PATHS) -> str:
    paths: set[Path] = set()
    for relative in inputs:
        target = kit / relative
        try:
            target.lstat()
        except OSError as error:
            fail(f"module SDK input is missing: {relative}: {error}")
        paths.add(target)
        if target.is_dir() and not target.is_symlink():
            for directory, names, files in os.walk(target, followlinks=False):
                names.sort()
                files.sort()
                base = Path(directory)
                paths.update(base / n for n in names)
                paths.update(base / n for n in files)
    digest = hashlib.sha256(b"rklings-module-sdk-v1\0")
    for path in sorted(paths, key=lambda v: v.relative_to(kit).as_posix()):
        relative = path.relative_to(kit).as_posix().encode("utf-8")
        metadata = path.lstat()
        digest.update(len(relative).to_bytes(8, "big"))
        digest.update(relative)
        digest.update((metadata.st_mode & 0o777).to_bytes(4, "big"))
        if path.is_symlink():
            target = os.readlink(path).encode("utf-8")
            digest.update(b"L" + len(target).to_bytes(8, "big") + target)
        elif path.is_dir():
            digest.update(b"D")
        elif path.is_file():
            digest.update(b"F" + metadata.st_size.to_bytes(8, "big"))
            with path.open("rb") as source:
                for block in iter(lambda: source.read(1024 * 1024), b""):
                    digest.update(block)
        else:
            fail(f"module SDK input is a special file: {path.relative_to(kit)}")
    return digest.hexdigest()


def fsync_directory(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def require_build_tools(manifest: dict[str, object]) -> None:
    compiler = str(manifest["compiler"])
    sp = _search_path()
    missing = [c for c in (compiler, *_BUILD_TOOLS) if shutil.which(c, path=sp) is None]
    if missing:
        fail("kernel build tools are missing: " + ", ".join(missing))
    probe = subprocess.run(
        [compiler, "-x", "c", "-o", os.devnull, "-", "-lelf"],
        input='#include <libelf.h>\n#include <gelf.h>\n'
              'int main(void) { return elf_version(EV_CURRENT) == EV_NONE; }\n',
        text=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        env={"PATH": sp, "LC_ALL": "C"},
    )
    if probe.returncode:
        fail("libelf development headers or linker library are missing")


def read_stamp(path: Path) -> dict[str, object] | None:
    if path.is_symlink() or not path.is_file():
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def _is_hex64(v: object) -> bool:
    return isinstance(v, str) and bool(_HEX64.fullmatch(v))


def ready(manifest: dict[str, object], paths: dict[str, Path | str]) -> tuple[bool, str]:
    kit = paths["kit"]
    assert isinstance(kit, Path)
    if kit.is_symlink():
        return False, "kernel-kit directory is a symlink"
    stamp = read_stamp(paths["stamp"])
    if stamp is None:
        return False, "not built"
    wanted = {
        "schema_version": 2, "release": paths["release"],
        "source_sha256": manifest["source_sha256"],
        "build_identity": paths["build_identity"],
    }
    if any(stamp.get(k) != v for k, v in wanted.items()):
        return False, "build inputs changed"
    if not _is_hex64(stamp.get("config_sha256")):
        return False, "build stamp lacks the final configuration digest"
    if not _is_hex64(stamp.get("sdk_sha256")):
        return False, "build stamp lacks the module SDK digest"
    artifact_hashes = stamp.get("artifact_sha256")
    if (not isinstance(artifact_hashes, dict)
            or set(artifact_hashes) != set(HASHED_BUILD_FILES)
            or not all(_is_hex64(v) for v in artifact_hashes.values())):
        return False, "build stamp lacks the artifact digest set"
    for label in ("source", "build"):
        p = paths[label]
        assert isinstance(p, Path)
        if p.is_symlink() or not p.is_dir():
            return False, f"missing {label} directory"
    build = paths["build"]
    assert isinstance(build, Path)
    for rel in REQUIRED_BUILD_FILES:
        if not regular_nonempty(build / rel):
            return False, f"missing or empty build artifact: {rel}"
    if sha256_file(build / ".config") != stamp["config_sha256"]:
        return False, "final kernel configuration changed"
    for rel in HASHED_BUILD_FILES:
        if sha256_file(build / rel) != artifact_hashes[rel]:
            return False, f"build artifact changed: {rel}"
    try:
        current = sdk_digest(kit)
    except KernelError as error:
        return False, str(error)
    if current != stamp["sdk_sha256"]:
        return False, "module SDK inputs changed"
    try:
        verify_config(build / ".config", ROOT / str(manifest["config"]),
                      str(manifest["local_version"]))
    except KernelError as error:
        return False, str(error)
    result = subprocess.run(
        ["make", "-s", "-C", str(build), "kernelrelease"], check=False,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        env=build_environment(manifest),
    )
    if result.returncode or result.stdout.strip() != paths["release"]:
        return False, "kernel release does not match the manifest"
    return True, "ready"


def download(manifest: dict[str, object], paths: dict[str, Path | str]) -> Path:
    archive = paths["archive"]
    url = paths["url"]
    assert isinstance(archive, Path) and isinstance(url, str)
    _ensure_dir(archive.parent, create=True)
    expected = str(manifest["source_sha256"])
    expected_size = int(manifest["source_size"])
    if (regular_nonempty(archive) and archive.stat().st_size == expected_size
            and sha256_file(archive) == expected):
        UI.success(f"Source archive verified: {archive}")
        return archive
    if archive.exists() or archive.is_symlink():
        if archive.is_dir() and not archive.is_symlink():
            fail(f"source cache path is a directory: {archive}")
        archive.unlink()
    temporary = archive.with_name(archive.name + ".part")
    if temporary.exists() or temporary.is_symlink():
        temporary.unlink()
    total_mib = expected_size / (1024 * 1024)
    UI.progress(f"Downloading {total_mib:.0f} MiB from {url}")
    show_progress = _progress_capable()
    try:
        with urllib.request.urlopen(url, timeout=30) as response, temporary.open("xb") as target:
            header = response.headers.get("Content-Length")
            if header is not None and (not header.isdigit() or int(header) != expected_size):
                fail(f"kernel download size header is {header}, expected {expected_size}")
            received = 0
            dl_start = time.monotonic()
            while block := response.read(min(1024 * 1024, expected_size + 1 - received)):
                received += len(block)
                if received > expected_size:
                    fail("kernel download exceeded its pinned size")
                target.write(block)
                if show_progress:
                    pct = received * 100 // expected_size
                    mib = received / (1024 * 1024)
                    elapsed = time.monotonic() - dl_start
                    speed = mib / elapsed if elapsed > 0.1 else 0
                    eta = (total_mib - mib) / speed if speed > 0 else 0
                    filled = pct * 30 // 100
                    bar = "#" * filled + "-" * (30 - filled)
                    sys.stdout.write(
                        f"\r  [{bar}] {pct:3d}%  {mib:.0f}/{total_mib:.0f} MiB  "
                        f"{speed:.1f} MiB/s  ETA {_format_eta(eta)}")
                    sys.stdout.flush()
            if show_progress:
                elapsed = time.monotonic() - dl_start
                sys.stdout.write("\r\x1b[2K")
                sys.stdout.flush()
                UI.line(f"({total_mib:.0f} MiB in {_format_eta(elapsed)})", style="dim")
            target.flush()
            os.fsync(target.fileno())
    except (KernelError, OSError, urllib.error.URLError) as error:
        if temporary.exists():
            temporary.unlink()
        if isinstance(error, KernelError):
            raise
        fail(f"kernel download failed: {error}")
    actual_size = temporary.stat().st_size
    if actual_size != expected_size:
        temporary.unlink()
        fail(f"kernel archive size mismatch: expected {expected_size}, got {actual_size}")
    actual = sha256_file(temporary)
    if actual != expected:
        temporary.unlink()
        fail(f"kernel archive checksum mismatch: expected {expected}, got {actual}")
    os.replace(temporary, archive)
    fsync_directory(archive.parent)
    UI.success(f"Source archive verified: {archive}")
    return archive


def safe_members(archive: tarfile.TarFile, version: str) -> list[tarfile.TarInfo]:
    root = f"linux-{version}"
    members = archive.getmembers()
    if len(members) > MAX_ARCHIVE_MEMBERS:
        fail("kernel archive contains too many members")
    unpacked_size = 0
    names: set[str] = set()
    for m in members:
        path = PurePosixPath(m.name)
        if (path.is_absolute() or not path.parts or path.parts[0] != root
                or ".." in path.parts or m.name in names):
            fail(f"unsafe path in kernel archive: {m.name}")
        names.add(m.name)
        if not (m.isdir() or m.isfile() or m.issym() or m.islnk()):
            fail(f"unsupported special file in kernel archive: {m.name}")
        unpacked_size += m.size
        if unpacked_size > MAX_UNPACKED_SIZE:
            fail("kernel archive exceeds the unpacked-size limit")
        if m.issym() or m.islnk():
            target = PurePosixPath(m.linkname)
            if target.is_absolute():
                fail(f"absolute link in kernel archive: {m.name}")
            base = path.parent if m.issym() else PurePosixPath()
            parts: list[str] = []
            for part in (*base.parts, *target.parts):
                if part in ("", "."):
                    continue
                if part == "..":
                    if not parts:
                        fail(f"escaping link in kernel archive: {m.name}")
                    parts.pop()
                else:
                    parts.append(part)
            if not parts or parts[0] != root:
                fail(f"escaping link in kernel archive: {m.name}")
    return members


def run(
    command: list[str], *, cwd: Path | None = None,
    env: dict[str, str] | None = None, live: bool = False,
    expected_steps: int = 0, quiet: bool = False,
) -> None:
    shown = shlex.join(command)
    if not quiet:
        UI.transcript(command)
    if live and _progress_capable() and expected_steps > 0:
        _run_with_progress(command, cwd=cwd, env=env, shown=shown,
                           expected_steps=expected_steps)
    elif live:
        result = subprocess.run(command, cwd=cwd, env=env, check=False,
                                stderr=subprocess.STDOUT)
        UI.process_result(result.returncode)
        if result.returncode:
            fail(f"command failed with status {result.returncode}: {shown}")
    else:
        result = subprocess.run(command, cwd=cwd, env=env, check=False, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if not quiet:
            UI.process_output(result.stdout or "", error=bool(result.returncode))
            UI.process_result(result.returncode)
        elif result.returncode:
            UI.process_output(result.stdout or "", error=True)
            UI.process_result(result.returncode)
        if result.returncode:
            fail(f"command failed with status {result.returncode}: {shown}")


def _count_config_objects(config_path: Path) -> int:
    try:
        text = config_path.read_text()
    except OSError:
        return 0
    return sum(1 for l in text.splitlines()
               if l.endswith(("=y", "=m")) and not l.startswith("#"))


def _progress_capable() -> bool:
    return (sys.stdout.isatty()
            and "NO_COLOR" not in os.environ
            and os.environ.get("TERM") != "dumb")


def _format_eta(seconds: float) -> str:
    if seconds <= 0:
        return "0s"
    if seconds < 60:
        return f"{int(seconds)}s"
    return f"{int(seconds) // 60}m{int(seconds) % 60:02d}s"


def _run_with_progress(
    command: list[str], *, cwd: Path | None = None,
    env: dict[str, str] | None = None, shown: str, expected_steps: int,
) -> None:
    proc = subprocess.Popen(command, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert proc.stdout is not None
    steps = 0
    start = time.monotonic()
    _STEP_PREFIXES = ("CC ", "CC\t", "LD ", "LD\t", "AR ", "AR\t", "OBJCOPY ")
    for raw in proc.stdout:
        line = raw.lstrip()
        if line.startswith(_STEP_PREFIXES):
            steps += 1
        if not _progress_capable():
            continue
        elapsed = time.monotonic() - start
        rate = steps / elapsed if elapsed > 0.5 else 0
        if expected_steps > 0:
            pct = min(steps * 100 // expected_steps, 99)
            filled = pct * 30 // 100
            bar = "#" * filled + "-" * (30 - filled)
            remaining = max(expected_steps - steps, 0)
            eta = f"ETA ~{_format_eta(remaining / rate)}" if rate > 1 and remaining > 0 else ""
            sys.stdout.write(f"\r\x1b[2K  [{bar}] {pct:3d}%  {steps}/~{expected_steps}  {eta}")
        else:
            sys.stdout.write(f"\r\x1b[2K  {steps} steps  {rate:.0f}/s  {_format_eta(elapsed)}")
        sys.stdout.flush()
    if _progress_capable():
        sys.stdout.write("\r\x1b[2K")
        sys.stdout.flush()
    rc = proc.wait()
    elapsed = time.monotonic() - start
    UI.line(f"({'build failed after' if rc else ''}{steps} steps in {_format_eta(elapsed)})",
            style="dim")
    UI.process_result(rc)
    if rc:
        fail(f"command failed with status {rc}: {shown}")


def available_memory() -> int | None:
    try:
        for line in Path("/proc/meminfo").read_text(encoding="ascii").splitlines():
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) * 1024
    except (OSError, ValueError, IndexError):
        pass
    return None


def available_cpus() -> int:
    try:
        cpus = len(os.sched_getaffinity(0))
    except AttributeError:
        cpus = os.cpu_count() or 1
    # Respect cgroup CPU quotas (Docker, Codespaces)
    for cg_path in ("/sys/fs/cgroup/cpu.max", "/sys/fs/cgroup/cpu/cpu.cfs_quota_us"):
        try:
            text = Path(cg_path).read_text(encoding="ascii").strip()
            if cg_path.endswith("cpu.max"):
                quota_s, period_s = text.split()
                if quota_s != "max":
                    cpus = min(cpus, max(1, (int(quota_s) + int(period_s) - 1) // int(period_s)))
            else:
                quota = int(text)
                if quota > 0:
                    period = int(Path(cg_path.replace("quota", "period")).read_text().strip())
                    cpus = min(cpus, max(1, (quota + period - 1) // period))
        except (OSError, ValueError):
            pass
    return max(1, cpus)


def parallel_jobs() -> int:
    cpus = available_cpus()
    memory = available_memory()
    memory_jobs = max(1, memory // (1536 * 1024 * 1024)) if memory is not None else cpus
    safe_limit = max(1, min(cpus, memory_jobs))
    override = os.getenv("RKLINGS_JOBS")
    if override is not None:
        if not override.isdigit() or int(override) < 1:
            fail("RKLINGS_JOBS must be a positive integer")
        if int(override) > cpus:
            fail(f"RKLINGS_JOBS exceeds the available CPU limit ({cpus})")
        return int(override)
    return safe_limit


def config_contract(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            result[key] = value
    return result


def verify_config(config: Path, fragment: Path, local_version: str) -> None:
    actual = config_contract(config)
    wanted = config_contract(fragment)
    wanted["CONFIG_LOCALVERSION"] = json.dumps(local_version)
    for key, value in wanted.items():
        if value == "n":
            if actual.get(key) not in (None, "n"):
                fail(f"course kernel configuration did not disable {key}")
        elif value == '""':
            if actual.get(key) not in (None, '""'):
                fail(f"course kernel configuration did not keep {key} empty")
        elif actual.get(key) != value:
            fail(f"course kernel configuration did not preserve {key}={value}")
    for key, value in REQUIRED_FINAL_CONFIG.items():
        if actual.get(key) != value:
            fail(f"course kernel configuration is missing required {key}={value}")


def build_environment(manifest: dict[str, object], home: Path | None = None) -> dict[str, str]:
    result = {
        "PATH": _search_path(), "LC_ALL": "C", "LANG": "C", "TZ": "UTC",
        "ARCH": "x86_64", "CC": str(manifest["compiler"]),
        "HOSTCC": str(manifest["compiler"]),
        "KBUILD_BUILD_TIMESTAMP": "1970-01-01T00:00:00Z",
        "KBUILD_BUILD_USER": "rklings", "KBUILD_BUILD_HOST": "course",
        "KBUILD_BUILD_VERSION": "1", "SOURCE_DATE_EPOCH": "0",
    }
    if home is not None:
        home.mkdir(parents=True, exist_ok=True)
        result["HOME"] = str(home)
    if "TMPDIR" in os.environ:
        result["TMPDIR"] = os.environ["TMPDIR"]
    return result


def preflight_space() -> None:
    _ensure_dir(RUNTIME, create=True)
    free = shutil.disk_usage(RUNTIME).free
    if free < MIN_BUILD_SPACE:
        fail(f"course-kernel build needs at least 4 GiB free; "
             f"only {free / 1024**3:.1f} GiB is available")


def remove_generated_kit(path: Path) -> None:
    kernel_root = RUNTIME / "kernel/kits"
    if path.parent != kernel_root or path.is_symlink() or not path.is_dir():
        fail(f"refusing to remove unsafe kernel-kit path: {path}")
    shutil.rmtree(path)


def begin_kit_build(kit: Path) -> Path | None:
    _ensure_dir(kit.parent, create=True)
    backup: Path | None = None
    if kit.exists() or kit.is_symlink():
        if kit.is_symlink() or not kit.is_dir():
            fail(f"refusing to replace unsafe kernel-kit path: {kit}")
        backup = kit.with_name(kit.name + ".previous")
        if backup.exists() or backup.is_symlink():
            fail(f"unfinished kernel-kit transaction needs recovery: {backup}")
        os.replace(kit, backup)
    try:
        kit.mkdir()
    except OSError:
        if backup is not None:
            os.replace(backup, kit)
        raise
    fsync_directory(kit.parent)
    return backup


def rollback_kit_build(kit: Path, backup: Path | None) -> None:
    if kit.exists() or kit.is_symlink():
        remove_generated_kit(kit)
    if backup is not None:
        os.replace(backup, kit)
    fsync_directory(kit.parent)


def finish_kit_build(kit: Path, backup: Path | None) -> None:
    fsync_directory(kit)
    if backup is not None:
        remove_generated_kit(backup)
    fsync_directory(kit.parent)


def prune_superseded_kits(current: Path, release: str) -> tuple[Path, ...]:
    kernel_root = RUNTIME / "kernel/kits"
    pattern = re.compile(rf"{re.escape(release)}-[0-9a-f]{{16}}\Z")
    if (current.parent != kernel_root or not pattern.fullmatch(current.name)
            or current.is_symlink() or not current.is_dir()):
        fail(f"refusing to prune from unsafe current kernel kit: {current}")
    removed: list[Path] = []
    for candidate in sorted(kernel_root.iterdir()):
        if candidate == current or not pattern.fullmatch(candidate.name):
            continue
        if candidate.is_symlink() or not candidate.is_dir():
            continue
        remove_generated_kit(candidate)
        removed.append(candidate)
    if removed:
        fsync_directory(kernel_root)
    return tuple(removed)


def recover_interrupted_build(
    manifest: dict[str, object], paths: dict[str, Path | str],
) -> None:
    kit = paths["kit"]
    assert isinstance(kit, Path)
    backup = kit.with_name(kit.name + ".previous")
    if not backup.exists() and not backup.is_symlink():
        return
    if backup.is_symlink() or not backup.is_dir():
        fail(f"kernel-kit recovery path is unsafe: {backup}")
    if ready(manifest, paths)[0]:
        remove_generated_kit(backup)
    else:
        if kit.exists() or kit.is_symlink():
            remove_generated_kit(kit)
        os.replace(backup, kit)
    fsync_directory(kit.parent)


def build(manifest: dict[str, object], paths: dict[str, Path | str]) -> None:
    archive = download(manifest, paths)
    _ensure_dir(RUNTIME / "kernel/kits", create=True)
    kit = paths["kit"]
    assert isinstance(kit, Path)
    backup = begin_kit_build(kit)
    try:
        if not hasattr(tarfile, "data_filter"):
            fail("Python tar extraction filters are required; use Python 3.12 or newer")
        with tarfile.open(archive, mode="r:xz") as source:
            members = safe_members(source, str(manifest["version"]))
            total = len(members)
            show = _progress_capable()
            UI.progress(f"Extracting Linux {manifest['version']} ({total} files)")
            start = time.monotonic()
            for i, member in enumerate(members, 1):
                source.extract(member, kit, filter="data")
                if show and i % 1000 == 0:
                    pct = i * 100 // total
                    filled = pct * 30 // 100
                    bar = "#" * filled + "-" * (30 - filled)
                    elapsed = time.monotonic() - start
                    eta = elapsed / i * (total - i) if i else 0
                    sys.stdout.write(
                        f"\r  [{bar}] {pct:3d}%  {i}/{total}  "
                        f"ETA {_format_eta(eta)}")
                    sys.stdout.flush()
            if show:
                elapsed = time.monotonic() - start
                sys.stdout.write("\r\x1b[2K")
                sys.stdout.flush()
                UI.line(f"({total} files in {_format_eta(elapsed)})", style="dim")
        (kit / f"linux-{manifest['version']}").rename(kit / "source")
        build_tree = kit / "build"
        build_tree.mkdir()
        env = build_environment(manifest, build_tree / ".home")
        make = ["make", "-s", "-C", str(kit / "source"), f"O={build_tree}"]
        show = _progress_capable()
        config_steps = ["base config", "merge fragment", "local version", "olddefconfig"]
        UI.progress("Configuring kernel")
        config_commands = [
            (make + [str(manifest["base_config"])], dict(env=env, quiet=True)),
            ([str(kit / "source/scripts/kconfig/merge_config.sh"), "-m", "-O",
              str(build_tree), str(build_tree / ".config"),
              str(ROOT / str(manifest["config"]))],
             dict(cwd=kit / "source", env=env, quiet=True)),
            ([str(kit / "source/scripts/config"), "--file",
              str(build_tree / ".config"), "--set-str", "LOCALVERSION",
              str(manifest["local_version"])], dict(env=env, quiet=True)),
            (make + ["olddefconfig"], dict(env=env, quiet=True)),
        ]
        for i, (cmd, kwargs) in enumerate(config_commands):
            if show:
                sys.stdout.write(f"\r  [{i+1}/{len(config_steps)}] {config_steps[i]}")
                sys.stdout.flush()
            run(cmd, **kwargs)
        if show:
            sys.stdout.write("\r\x1b[2K")
            sys.stdout.flush()
        verify_config(build_tree / ".config",
                      ROOT / str(manifest["config"]),
                      str(manifest["local_version"]))
        jobs = parallel_jobs()
        make_build = ["make", "-C", str(kit / "source"), f"O={build_tree}"]
        estimated = _count_config_objects(build_tree / ".config") * 2
        UI.progress(f"Building the course kernel (~{estimated} steps, {jobs} parallel jobs)")
        run(make_build + [f"-j{jobs}", "bzImage", "modules"],
            env=env, live=True, expected_steps=estimated)
        release = subprocess.run(
            make + ["-s", "kernelrelease"], env=env, check=True,
            text=True, stdout=subprocess.PIPE,
        ).stdout.strip()
        if release != paths["release"]:
            fail(f"built kernel release is {release}, expected {paths['release']}")
        for rel in REQUIRED_BUILD_FILES:
            if not regular_nonempty(build_tree / rel):
                fail(f"kernel build did not produce a usable {rel}")
        stamp = {
            "schema_version": 2, "release": paths["release"],
            "source_sha256": manifest["source_sha256"],
            "build_identity": paths["build_identity"],
            "config_sha256": sha256_file(build_tree / ".config"),
            "sdk_sha256": sdk_digest(kit),
            "artifact_sha256": {
                rel: sha256_file(build_tree / rel) for rel in HASHED_BUILD_FILES
            },
        }
        (kit / "stamp.json").write_text(
            json.dumps(stamp, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    except BaseException:
        rollback_kit_build(kit, backup)
        raise
    finish_kit_build(kit, backup)


def ensure(manifest: dict[str, object], paths: dict[str, Path | str]) -> None:
    lock_path = RUNTIME / "kernel/.lock"
    _ensure_dir(lock_path.parent, create=True)
    with lock_path.open("a+b") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        recover_interrupted_build(manifest, paths)
        is_ready, reason = ready(manifest, paths)
        if is_ready:
            UI.success(f"Course kernel ready: {paths['release']}")
            return
        UI.progress(f"Course kernel needs preparation: {reason}")
        preflight_space()
        UI.progress(
            f"Pinned source: {int(manifest['source_size']) / 1024**2:.0f} MiB; "
            "build workspace requires at least 4 GiB free")
        build(manifest, paths)
        is_ready, reason = ready(manifest, paths)
        if not is_ready:
            fail(f"new course kernel failed verification: {reason}")
        kit = paths["kit"]
        assert isinstance(kit, Path)
        removed = prune_superseded_kits(kit, str(paths["release"]))
        if removed:
            UI.success(f"Removed {len(removed)} superseded kernel kit(s)")
        UI.success(f"Course kernel ready: {paths['release']}")


def existing_path(manifest: dict[str, object], paths: dict[str, Path | str], key: str) -> None:
    is_ready, reason = ready(manifest, paths)
    if not is_ready:
        fail(f"course kernel is not ready ({reason}); run: scripts/kernel ensure")
    print(paths[key])


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument(
        "command",
        choices=("ensure", "status", "release", "build-dir", "image", "vmlinux"),
    )
    return result


def main() -> int:
    arguments = parser().parse_args()
    manifest = read_manifest()
    if arguments.command == "release":
        print(release_name(manifest))
        return 0
    if arguments.command == "ensure":
        signal.signal(signal.SIGTERM, interrupt_for_cleanup)
        require_build_tools(manifest)
    paths = layout(manifest)
    if arguments.command == "ensure":
        ensure(manifest, paths)
    elif arguments.command == "status":
        is_ready, reason = ready(manifest, paths)
        print(f"{'ready' if is_ready else 'missing'}\t{paths['release']}\t{reason}")
        if not is_ready:
            return 1
    elif arguments.command == "build-dir":
        existing_path(manifest, paths, "build")
    elif arguments.command == "image":
        existing_path(manifest, paths, "image")
    elif arguments.command == "vmlinux":
        existing_path(manifest, paths, "vmlinux")
    return 0


def entrypoint() -> None:
    try:
        raise SystemExit(main())
    except KernelError as error:
        UI.error(str(error))
        raise SystemExit(1)
    except (OSError, subprocess.SubprocessError, tarfile.TarError) as error:
        UI.error(f"course-kernel preparation failed: {error}")
        raise SystemExit(1)
    except KeyboardInterrupt:
        UI.error("course-kernel preparation interrupted")
        raise SystemExit(130)


if __name__ == "__main__":
    entrypoint()
