#!/usr/bin/env python3
"""Build and locate the pinned Rootkitlings guest kernel."""

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
import tomllib
import urllib.error
import urllib.request


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from engine.console import Console  # noqa: E402


UI = Console("rootkitlings.kernel")
RUNTIME = ROOT / ".rootkitlings"
TOOLBOX = ROOT / "scripts/toolbox"
MANIFEST_PATH = ROOT / "vm/kernel.toml"
# Increment only when the extraction, configuration, build environment, or
# required-artifact recipe changes. Presentation and diagnostics do not affect
# the kernel artifacts and must not invalidate multi-gigabyte kits.
BUILD_RECIPE_REVISION = 1
MANIFEST_FIELDS = {
    "schema_version",
    "architecture",
    "version",
    "local_version",
    "compiler",
    "base_config",
    "source_base_url",
    "source_sha256",
    "source_size",
    "config",
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
    "arch/x86/boot/bzImage",
    "vmlinux",
    "System.map",
    "Module.symvers",
    ".config",
    "include/config/auto.conf",
    "include/generated/autoconf.h",
    "scripts/module.lds",
)
HASHED_BUILD_FILES = REQUIRED_BUILD_FILES
SDK_INPUT_PATHS = (
    "source/Makefile",
    "source/Kbuild",
    "source/include",
    "source/arch/x86/Makefile",
    "source/arch/x86/Makefile_32.cpu",
    "source/arch/x86/include",
    "source/scripts",
    "build/Makefile",
    "build/include",
    "build/arch/x86/include",
    "build/scripts",
    "build/tools/objtool",
)


class KernelError(RuntimeError):
    pass


def fail(message: str) -> None:
    raise KernelError(message)


def interrupt_for_cleanup(_signum: int, _frame: object) -> None:
    raise KeyboardInterrupt


def validate_repository_path(path: Path) -> None:
    value = os.fspath(path)
    if not re.fullmatch(r"/[A-Za-z0-9._+/-]*", value):
        fail(
            "the repository path may contain only ASCII letters, digits, "
            "'/', '.', '_', '+', and '-' (required by Kbuild)"
        )


def read_manifest() -> dict[str, object]:
    validate_repository_path(ROOT)
    ensure_directory_chain(MANIFEST_PATH.parent, create=False)
    try:
        raw = MANIFEST_PATH.read_bytes()
        data = tomllib.loads(raw.decode("utf-8"))
    except (OSError, UnicodeDecodeError, tomllib.TOMLDecodeError) as error:
        fail(f"cannot read {MANIFEST_PATH.relative_to(ROOT)}: {error}")
    if set(data) != MANIFEST_FIELDS:
        fail("vm/kernel.toml fields do not match schema 1")
    if type(data["schema_version"]) is not int or data["schema_version"] != 1:
        fail("vm/kernel.toml schema_version must be integer 1")
    for field in MANIFEST_FIELDS - {"schema_version", "source_size"}:
        value = data[field]
        if not isinstance(value, str) or not value or any(ord(char) < 32 for char in value):
            fail(f"vm/kernel.toml {field} must be a nonempty printable string")
    if data["architecture"] != "x86_64":
        fail("only the x86_64 course kernel is currently supported")
    if data["base_config"] != "allnoconfig":
        fail("vm/kernel.toml base_config must be allnoconfig")
    version = str(data["version"])
    if not all(part.isdigit() for part in version.split(".")) or len(version.split(".")) != 3:
        fail("vm/kernel.toml version must have three numeric components")
    local_version = str(data["local_version"])
    if not re.fullmatch(r"-[A-Za-z0-9][A-Za-z0-9-]*", local_version):
        fail("vm/kernel.toml local_version must be a simple dash-prefixed suffix")
    checksum = str(data["source_sha256"])
    if len(checksum) != 64 or any(char not in "0123456789abcdef" for char in checksum):
        fail("vm/kernel.toml source_sha256 must be a lowercase SHA-256 digest")
    if type(data["source_size"]) is not int or not 1024 <= data["source_size"] <= 1024**3:
        fail("vm/kernel.toml source_size must be an integer between 1 KiB and 1 GiB")
    relative_config = PurePosixPath(str(data["config"]))
    if relative_config.is_absolute() or ".." in relative_config.parts:
        fail("vm/kernel.toml config must stay inside the repository")
    config = ROOT.joinpath(*relative_config.parts)
    ensure_directory_chain(config.parent, create=False)
    if config.is_symlink() or not config.is_file():
        fail(f"kernel config is not a regular file: {config.relative_to(ROOT)}")
    return data


def release_name(manifest: dict[str, object]) -> str:
    return str(manifest["version"]) + str(manifest["local_version"])


def ensure_directory_chain(path: Path, *, create: bool) -> None:
    try:
        relative = path.relative_to(ROOT)
    except ValueError:
        fail(f"managed path leaves the repository: {path}")
    current = ROOT
    for component in relative.parts:
        current /= component
        try:
            info = current.lstat()
        except FileNotFoundError:
            if not create:
                return
            try:
                current.mkdir()
            except OSError as error:
                fail(f"cannot create managed directory {current}: {error}")
            info = current.lstat()
        if current.is_symlink() or not current.is_dir():
            fail(f"managed directory is not a real directory: {current}")


def regular_nonempty(path: Path) -> bool:
    try:
        return not path.is_symlink() and path.is_file() and path.stat().st_size > 0
    except OSError:
        return False


def tool_identity(command: str) -> str:
    search_path = os.fspath(TOOLBOX) + os.pathsep + os.environ.get("PATH", os.defpath)
    resolved = shutil.which(command, path=search_path)
    if not resolved:
        return f"missing:{command}"
    result = subprocess.run(
        [resolved, "--version"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env={
            "PATH": search_path,
            "LC_ALL": "C",
            "LANG": "C",
        },
    )
    if result.returncode or not result.stdout.strip():
        fail(f"cannot identify build tool: {command}")
    first_line = next(line for line in result.stdout.splitlines() if line.strip())
    return f"{Path(resolved).resolve()}\n{first_line}"


def libelf_identity(compiler: str) -> str:
    result = subprocess.run(
        [compiler, "-print-file-name=libelf.so"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env={"PATH": os.environ.get("PATH", os.defpath), "LC_ALL": "C", "LANG": "C"},
    )
    candidate = Path(result.stdout.strip())
    if result.returncode or not candidate.is_absolute() or not candidate.is_file():
        fail("cannot identify the libelf linker library")
    return f"{candidate.resolve()}\n{sha256_file(candidate)}"


def build_identity(manifest: dict[str, object]) -> str:
    digest = hashlib.sha256(b"rootkitlings-kernel-build-v1\0")
    revision = str(BUILD_RECIPE_REVISION).encode("ascii")
    digest.update(len(revision).to_bytes(8, "big"))
    digest.update(revision)
    for path in (
        MANIFEST_PATH,
        ROOT / str(manifest["config"]),
        TOOLBOX / "bc",
    ):
        value = path.read_bytes()
        digest.update(len(value).to_bytes(8, "big"))
        digest.update(value)
    for command in (
        str(manifest["compiler"]), "bash", "make", "ld", "as", "ar", "nm",
        "objcopy", "objdump", "readelf", "bc", "bison", "flex", "perl", "gzip",
    ):
        value = tool_identity(command).encode()
        digest.update(len(value).to_bytes(8, "big"))
        digest.update(value)
    value = libelf_identity(str(manifest["compiler"])).encode()
    digest.update(len(value).to_bytes(8, "big"))
    digest.update(value)
    return digest.hexdigest()


def layout(manifest: dict[str, object]) -> dict[str, Path | str]:
    version = str(manifest["version"])
    release = release_name(manifest)
    identity = build_identity(manifest)
    kit = RUNTIME / "kernel/kits" / f"{release}-{identity[:16]}"
    filename = f"linux-{version}.tar.xz"
    return {
        "release": release,
        "build_identity": identity,
        "kit": kit,
        "source": kit / "source",
        "build": kit / "build",
        "image": kit / "build/arch/x86/boot/bzImage",
        "vmlinux": kit / "build/vmlinux",
        "stamp": kit / "stamp.json",
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
                paths.update(base / name for name in names)
                paths.update(base / name for name in files)
    digest = hashlib.sha256(b"rootkitlings-module-sdk-v1\0")
    for path in sorted(paths, key=lambda value: value.relative_to(kit).as_posix()):
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
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def require_build_tools(manifest: dict[str, object]) -> None:
    required = (
        str(manifest["compiler"]),
        "bash",
        "make",
        "ld",
        "as",
        "ar",
        "nm",
        "objcopy",
        "objdump",
        "readelf",
        "bc",
        "bison",
        "flex",
        "perl",
        "gzip",
    )
    search_path = os.fspath(TOOLBOX) + os.pathsep + os.environ.get("PATH", os.defpath)
    missing = [
        command for command in required if shutil.which(command, path=search_path) is None
    ]
    if missing:
        fail("kernel build tools are missing: " + ", ".join(missing))
    compiler = str(manifest["compiler"])
    probe = subprocess.run(
        [compiler, "-x", "c", "-o", os.devnull, "-", "-lelf"],
        input=(
            "#include <libelf.h>\n#include <gelf.h>\n"
            "int main(void) { return elf_version(EV_CURRENT) == EV_NONE; }\n"
        ),
        text=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        env={"PATH": search_path, "LC_ALL": "C"},
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


def ready(manifest: dict[str, object], paths: dict[str, Path | str]) -> tuple[bool, str]:
    ensure_directory_chain(RUNTIME, create=False)
    kit = paths["kit"]
    assert isinstance(kit, Path)
    ensure_directory_chain(kit.parent, create=False)
    if kit.is_symlink():
        return False, "kernel-kit directory is a symlink"
    stamp_path = paths["stamp"]
    assert isinstance(stamp_path, Path)
    stamp = read_stamp(stamp_path)
    if stamp is None:
        return False, "not built"
    wanted = {
        "schema_version": 2,
        "release": paths["release"],
        "source_sha256": manifest["source_sha256"],
        "build_identity": paths["build_identity"],
    }
    if any(stamp.get(key) != value for key, value in wanted.items()):
        return False, "build inputs changed"
    if not isinstance(stamp.get("config_sha256"), str):
        return False, "build stamp lacks the final configuration digest"
    if not isinstance(stamp.get("sdk_sha256"), str) or not re.fullmatch(
        r"[0-9a-f]{64}", stamp["sdk_sha256"]
    ):
        return False, "build stamp lacks the module SDK digest"
    artifact_hashes = stamp.get("artifact_sha256")
    if (
        not isinstance(artifact_hashes, dict)
        or set(artifact_hashes) != set(HASHED_BUILD_FILES)
        or any(
            not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value)
            for value in artifact_hashes.values()
        )
    ):
        return False, "build stamp lacks the artifact digest set"
    for label in ("source", "build"):
        path = paths[label]
        assert isinstance(path, Path)
        if path.is_symlink() or not path.is_dir():
            return False, f"missing {label} directory"
    build = paths["build"]
    assert isinstance(build, Path)
    for relative in REQUIRED_BUILD_FILES:
        if not regular_nonempty(build / relative):
            return False, f"missing or empty build artifact: {relative}"
    if sha256_file(build / ".config") != stamp["config_sha256"]:
        return False, "final kernel configuration changed"
    for relative in HASHED_BUILD_FILES:
        if sha256_file(build / relative) != artifact_hashes[relative]:
            return False, f"build artifact changed: {relative}"
    try:
        current_sdk_digest = sdk_digest(kit)
    except KernelError as error:
        return False, str(error)
    if current_sdk_digest != stamp["sdk_sha256"]:
        return False, "module SDK inputs changed"
    try:
        verify_config(
            build / ".config",
            ROOT / str(manifest["config"]),
            str(manifest["local_version"]),
        )
    except KernelError as error:
        return False, str(error)
    result = subprocess.run(
        ["make", "-s", "-C", str(build), "kernelrelease"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=build_environment(manifest),
    )
    if result.returncode or result.stdout.strip() != paths["release"]:
        return False, "kernel release does not match the manifest"
    return True, "ready"


def download(manifest: dict[str, object], paths: dict[str, Path | str]) -> Path:
    archive = paths["archive"]
    url = paths["url"]
    assert isinstance(archive, Path) and isinstance(url, str)
    ensure_directory_chain(archive.parent, create=True)
    expected = str(manifest["source_sha256"])
    expected_size = int(manifest["source_size"])
    if (regular_nonempty(archive) and archive.stat().st_size == expected_size and
            sha256_file(archive) == expected):
        UI.success(f"Source archive verified: {archive}")
        return archive
    if archive.exists() or archive.is_symlink():
        if archive.is_dir() and not archive.is_symlink():
            fail(f"source cache path is a directory: {archive}")
        archive.unlink()
    temporary = archive.with_name(archive.name + ".part")
    if temporary.exists() or temporary.is_symlink():
        temporary.unlink()
    UI.progress(f"Downloading {url}")
    try:
        with urllib.request.urlopen(url, timeout=30) as response, temporary.open("xb") as target:
            header = response.headers.get("Content-Length")
            if header is not None and (not header.isdigit() or int(header) != expected_size):
                fail(f"kernel download size header is {header}, expected {expected_size}")
            received = 0
            while block := response.read(min(1024 * 1024, expected_size + 1 - received)):
                received += len(block)
                if received > expected_size:
                    fail("kernel download exceeded its pinned size")
                target.write(block)
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
        fail(
            f"kernel archive size mismatch: expected {expected_size}, "
            f"got {actual_size}"
        )
    actual = sha256_file(temporary)
    if actual != expected:
        temporary.unlink()
        fail(f"kernel archive checksum mismatch: expected {expected}, got {actual}")
    os.replace(temporary, archive)
    fsync_directory(archive.parent)
    UI.success(f"Source archive verified: {archive}")
    return archive


def normalized_archive_path(base: PurePosixPath, target: PurePosixPath) -> PurePosixPath:
    parts: list[str] = []
    for part in (*base.parts, *target.parts):
        if part in ("", "."):
            continue
        if part == "..":
            if not parts:
                fail("archive link escapes its extraction root")
            parts.pop()
        else:
            parts.append(part)
    return PurePosixPath(*parts)


def safe_members(archive: tarfile.TarFile, version: str) -> list[tarfile.TarInfo]:
    root = f"linux-{version}"
    members = archive.getmembers()
    if len(members) > MAX_ARCHIVE_MEMBERS:
        fail("kernel archive contains too many members")
    unpacked_size = 0
    names: set[str] = set()
    for member in members:
        path = PurePosixPath(member.name)
        if (path.is_absolute() or not path.parts or path.parts[0] != root or
                ".." in path.parts or member.name in names):
            fail(f"unsafe path in kernel archive: {member.name}")
        names.add(member.name)
        if not (member.isdir() or member.isfile() or member.issym() or member.islnk()):
            fail(f"unsupported special file in kernel archive: {member.name}")
        unpacked_size += member.size
        if unpacked_size > MAX_UNPACKED_SIZE:
            fail("kernel archive exceeds the unpacked-size limit")
        if member.issym():
            target = PurePosixPath(member.linkname)
            if target.is_absolute():
                fail(f"absolute link in kernel archive: {member.name}")
            resolved = normalized_archive_path(path.parent, target)
            if not resolved.parts or resolved.parts[0] != root:
                fail(f"escaping link in kernel archive: {member.name}")
        elif member.islnk():
            target = PurePosixPath(member.linkname)
            if target.is_absolute():
                fail(f"absolute link in kernel archive: {member.name}")
            resolved = normalized_archive_path(PurePosixPath(), target)
            if not resolved.parts or resolved.parts[0] != root:
                fail(f"escaping link in kernel archive: {member.name}")
    return members


def run(
    command: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> None:
    shown = shlex.join(command)
    UI.transcript(command)
    result = subprocess.run(
        command,
        cwd=cwd,
        env=env,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    UI.process_output(result.stdout or "", error=bool(result.returncode))
    UI.process_result(result.returncode)
    if result.returncode:
        fail(f"command failed with status {result.returncode}: {shown}")


def cgroup_memberships(text: str) -> dict[str, PurePosixPath]:
    result: dict[str, PurePosixPath] = {}
    for line in text.splitlines():
        try:
            _hierarchy, controllers, raw_path = line.split(":", 2)
        except ValueError:
            continue
        path = PurePosixPath(raw_path)
        if not path.is_absolute() or ".." in path.parts:
            continue
        if controllers:
            for controller in controllers.split(","):
                result[controller] = path
        else:
            result["unified"] = path
    return result


def current_cgroups() -> dict[str, PurePosixPath]:
    try:
        text = Path("/proc/self/cgroup").read_text(encoding="ascii")
    except OSError:
        return {}
    return cgroup_memberships(text)


def cgroup_ancestors(root: Path, membership: PurePosixPath) -> list[Path]:
    current = root.joinpath(*membership.parts[1:])
    result: list[Path] = []
    while current == root or root in current.parents:
        result.append(current)
        if current == root:
            break
        current = current.parent
    return result


def available_memory() -> int | None:
    values: list[int] = []
    try:
        for line in Path("/proc/meminfo").read_text(encoding="ascii").splitlines():
            if line.startswith("MemAvailable:"):
                values.append(int(line.split()[1]) * 1024)
                break
    except (OSError, ValueError, IndexError):
        pass
    memberships = current_cgroups()
    memory_locations: list[tuple[Path, str, str]] = []
    if "unified" in memberships:
        memory_locations.extend(
            (path, "memory.max", "memory.current")
            for path in cgroup_ancestors(Path("/sys/fs/cgroup"), memberships["unified"])
        )
    if "memory" in memberships:
        memory_locations.extend(
            (path, "memory.limit_in_bytes", "memory.usage_in_bytes")
            for path in cgroup_ancestors(
                Path("/sys/fs/cgroup/memory"), memberships["memory"]
            )
        )
    for base, maximum_name, current_name in memory_locations:
        try:
            maximum = (base / maximum_name).read_text(encoding="ascii").strip()
            current = int((base / current_name).read_text(encoding="ascii").strip())
            if maximum != "max":
                values.append(max(0, int(maximum) - current))
        except (OSError, ValueError):
            pass
    return min(values) if values else None


def available_cpus() -> int:
    try:
        cpus = len(os.sched_getaffinity(0))
    except AttributeError:
        cpus = os.cpu_count() or 1
    memberships = current_cgroups()
    if "unified" in memberships:
        for base in cgroup_ancestors(Path("/sys/fs/cgroup"), memberships["unified"]):
            try:
                quota_text, period_text = (base / "cpu.max").read_text(
                    encoding="ascii"
                ).split()
                if quota_text != "max":
                    quota = max(
                        1,
                        (int(quota_text) + int(period_text) - 1) // int(period_text),
                    )
                    cpus = min(cpus, quota)
            except (OSError, ValueError):
                pass
    if "cpu" in memberships:
        for base in cgroup_ancestors(Path("/sys/fs/cgroup/cpu"), memberships["cpu"]):
            try:
                quota_value = int((base / "cpu.cfs_quota_us").read_text(encoding="ascii"))
                period_value = int((base / "cpu.cfs_period_us").read_text(encoding="ascii"))
                if quota_value > 0:
                    quota = max(1, (quota_value + period_value - 1) // period_value)
                    cpus = min(cpus, quota)
            except (OSError, ValueError):
                pass
    return max(1, cpus)


def parallel_jobs() -> int:
    cpus = available_cpus()
    memory = available_memory()
    memory_jobs = max(1, memory // (1536 * 1024 * 1024)) if memory is not None else cpus
    safe_limit = max(1, min(cpus, memory_jobs))
    override = os.getenv("ROOTKITLINGS_JOBS")
    if override is not None:
        if not override.isdigit() or int(override) < 1:
            fail("ROOTKITLINGS_JOBS must be a positive integer")
        if int(override) > cpus:
            fail(f"ROOTKITLINGS_JOBS exceeds the available CPU limit ({cpus})")
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
    compiler = str(manifest["compiler"])
    result = {
        "PATH": os.fspath(TOOLBOX) + os.pathsep + os.environ.get("PATH", os.defpath),
        "LC_ALL": "C",
        "LANG": "C",
        "TZ": "UTC",
        "ARCH": "x86_64",
        "CC": compiler,
        "HOSTCC": compiler,
        "KBUILD_BUILD_TIMESTAMP": "1970-01-01T00:00:00Z",
        "KBUILD_BUILD_USER": "rootkitlings",
        "KBUILD_BUILD_HOST": "course",
        "KBUILD_BUILD_VERSION": "1",
        "SOURCE_DATE_EPOCH": "0",
    }
    if home is not None:
        home.mkdir(parents=True, exist_ok=True)
        result["HOME"] = str(home)
    if "TMPDIR" in os.environ:
        result["TMPDIR"] = os.environ["TMPDIR"]
    return result


def preflight_space() -> None:
    ensure_directory_chain(RUNTIME, create=True)
    free = shutil.disk_usage(RUNTIME).free
    if free < MIN_BUILD_SPACE:
        fail(
            "course-kernel build needs at least 4 GiB free; "
            f"only {free / 1024**3:.1f} GiB is available"
        )


def remove_generated_kit(path: Path) -> None:
    kernel_root = RUNTIME / "kernel/kits"
    if path.parent != kernel_root or path.is_symlink() or not path.is_dir():
        fail(f"refusing to remove unsafe kernel-kit path: {path}")
    shutil.rmtree(path)


def begin_kit_build(kit: Path) -> Path | None:
    ensure_directory_chain(kit.parent, create=True)
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
    """Remove complete older identities for the current kernel release."""
    kernel_root = RUNTIME / "kernel/kits"
    expected = re.compile(rf"{re.escape(release)}-[0-9a-f]{{16}}\Z")
    if (
        current.parent != kernel_root
        or not expected.fullmatch(current.name)
        or current.is_symlink()
        or not current.is_dir()
    ):
        fail(f"refusing to prune from unsafe current kernel kit: {current}")
    removed: list[Path] = []
    for candidate in sorted(kernel_root.iterdir()):
        if candidate == current or not expected.fullmatch(candidate.name):
            continue
        if candidate.is_symlink() or not candidate.is_dir():
            continue
        remove_generated_kit(candidate)
        removed.append(candidate)
    if removed:
        fsync_directory(kernel_root)
    return tuple(removed)


def recover_interrupted_build(
    manifest: dict[str, object], paths: dict[str, Path | str]
) -> None:
    kit = paths["kit"]
    assert isinstance(kit, Path)
    backup = kit.with_name(kit.name + ".previous")
    if not backup.exists() and not backup.is_symlink():
        return
    if backup.is_symlink() or not backup.is_dir():
        fail(f"kernel-kit recovery path is unsafe: {backup}")
    current_ready, _ = ready(manifest, paths)
    if current_ready:
        remove_generated_kit(backup)
    else:
        if kit.exists() or kit.is_symlink():
            remove_generated_kit(kit)
        os.replace(backup, kit)
    fsync_directory(kit.parent)


def build(manifest: dict[str, object], paths: dict[str, Path | str]) -> None:
    archive = download(manifest, paths)
    kernel_root = RUNTIME / "kernel/kits"
    ensure_directory_chain(kernel_root, create=True)
    kit = paths["kit"]
    assert isinstance(kit, Path)
    backup = begin_kit_build(kit)
    try:
        UI.progress(f"Extracting Linux {manifest['version']}")
        if not hasattr(tarfile, "data_filter"):
            fail("Python tar extraction filters are required; use a patched Python 3.11 or newer")
        with tarfile.open(archive, mode="r:xz") as source:
            members = safe_members(source, str(manifest["version"]))
            source.extractall(kit, members=members, filter="data")
        extracted = kit / f"linux-{manifest['version']}"
        source_tree = kit / "source"
        extracted.rename(source_tree)
        build_tree = kit / "build"
        build_tree.mkdir()
        environment = build_environment(manifest, build_tree / ".home")
        make = ["make", "-s", "-C", str(source_tree), f"O={build_tree}"]
        run(make + [str(manifest["base_config"])], env=environment)
        fragment = ROOT / str(manifest["config"])
        merge = source_tree / "scripts/kconfig/merge_config.sh"
        run(
            [str(merge), "-m", "-O", str(build_tree),
             str(build_tree / ".config"), str(fragment)],
            cwd=source_tree,
            env=environment,
        )
        config_tool = source_tree / "scripts/config"
        run(
            [str(config_tool), "--file", str(build_tree / ".config"),
             "--set-str", "LOCALVERSION", str(manifest["local_version"])],
            env=environment,
        )
        run(make + ["olddefconfig"], env=environment)
        verify_config(build_tree / ".config", fragment, str(manifest["local_version"]))
        jobs = parallel_jobs()
        UI.progress(f"Building the course kernel with {jobs} parallel job(s)")
        # The modules pass publishes Module.symvers for external-module modpost.
        run(make + [f"-j{jobs}", "bzImage", "modules"], env=environment)
        release = subprocess.run(
            make + ["-s", "kernelrelease"],
            env=environment,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
        ).stdout.strip()
        if release != paths["release"]:
            fail(f"built kernel release is {release}, expected {paths['release']}")
        for artifact in (
            build_tree / "arch/x86/boot/bzImage",
            build_tree / "vmlinux",
            build_tree / "Module.symvers",
        ):
            if not artifact.is_file() or artifact.is_symlink():
                fail(f"kernel build did not produce {artifact.relative_to(kit)}")
        for relative in REQUIRED_BUILD_FILES:
            if not regular_nonempty(build_tree / relative):
                fail(f"kernel build did not produce a usable {relative}")
        stamp = {
            "schema_version": 2,
            "release": paths["release"],
            "source_sha256": manifest["source_sha256"],
            "build_identity": paths["build_identity"],
            "config_sha256": sha256_file(build_tree / ".config"),
            "sdk_sha256": sdk_digest(kit),
            "artifact_sha256": {
                relative: sha256_file(build_tree / relative)
                for relative in HASHED_BUILD_FILES
            },
        }
        (kit / "stamp.json").write_text(
            json.dumps(stamp, sort_keys=True, indent=2) + "\n",
            encoding="utf-8",
        )
    except BaseException:
        rollback_kit_build(kit, backup)
        raise
    finish_kit_build(kit, backup)


def ensure(manifest: dict[str, object], paths: dict[str, Path | str]) -> None:
    lock_path = RUNTIME / "kernel/.lock"
    ensure_directory_chain(lock_path.parent, create=True)
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
            "Pinned source: "
            f"{int(manifest['source_size']) / 1024**2:.0f} MiB; "
            "build workspace requires at least 4 GiB free"
        )
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
    value = paths[key]
    print(value)


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
