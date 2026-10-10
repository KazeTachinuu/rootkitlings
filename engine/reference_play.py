"""Host-side control for completed reference demonstrations.

This module owns framed control I/O, local listeners, authenticated encryption,
and disposable-VM cleanup. Exercise manifests own the guest setup script and
select a protocol profile.
"""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import re
import secrets
import selectors
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from typing import Callable, TextIO


KEY_LENGTH = 32
NONCE_LENGTH = 12
TAG_LENGTH = 16
HEADER_LENGTH = 18
FRAME_MAX = 64
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
COMMAND_HELP = (
    "help | status | enable | disable | run | mark TEXT | shell | "
    "shell status | ping | info | quit\n"
)
CONTROL_TEXT_MAX = FRAME_MAX - HEADER_LENGTH - TAG_LENGTH
PLAY_READY = "ROOTKITLINGS_PLAY_READY"
PLAY_ENABLE_DONE = "ROOTKITLINGS_PLAY_ENABLE_DONE"


class OperatorError(RuntimeError):
    """The host operator or its disposable guest failed."""


def _serialized(callback, lock: threading.Lock):
    """Return a callback that cannot interleave with sibling callbacks."""
    def invoke(*args, **kwargs):
        with lock:
            return callback(*args, **kwargs)

    return invoke


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
        transcript: Callable[[list[object]], None],
        output: Callable[[str], None],
    ):
        if len(key) != KEY_LENGTH:
            raise OperatorError("control key must contain exactly 32 bytes")
        self._key = key
        self._helper = helper
        self._transcript = transcript
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
        self._transcript(command)
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


def _listener() -> socket.socket:
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    return listener


@dataclass
class HostOperator:
    key: bytes
    control_listener: socket.socket
    shell_listener: socket.socket

    @classmethod
    def create(cls) -> "HostOperator":
        key = bytes(KEY_LENGTH)
        while not any(key):
            key = secrets.token_bytes(KEY_LENGTH)
        return cls(key, _listener(), _listener())

    @property
    def control_port(self) -> int:
        return int(self.control_listener.getsockname()[1])

    @property
    def shell_port(self) -> int:
        return int(self.shell_listener.getsockname()[1])

    def close(self) -> None:
        self.control_listener.close()
        self.shell_listener.close()


def _build_crypto_helper(
    root: Path,
    directory: Path,
    transcript: Callable[[list[object]], None],
    output: Callable[[str], None],
) -> Path:
    helper = directory / "operator-aead"
    command = [
        "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
        "-o", str(helper), str(root / "engine/operator_aead.c"), "-lcrypto",
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
        raise OperatorError(
            "could not build operator crypto; run make doctor"
        )
    return helper


def _stop_process(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=2)
    except (ProcessLookupError, subprocess.TimeoutExpired):
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=2)


def _accept_while_running(
    listener: socket.socket,
    process: subprocess.Popen[str],
    timeout: float,
) -> socket.socket:
    deadline = time.monotonic() + timeout
    listener.settimeout(0.25)
    while time.monotonic() < deadline:
        if process.poll() is not None:
            status = process.returncode
            raise OperatorError(
                f"disposable guest exited before connecting ({status})"
            )
        try:
            peer, _ = listener.accept()
            return peer
        except TimeoutError:
            continue
    raise OperatorError("timed out waiting for the disposable guest")


def _wait_for_guest_event(
    event: threading.Event,
    process: subprocess.Popen[str],
    timeout: float,
    label: str,
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if event.wait(0.1):
            return
        if process.poll() is not None:
            raise OperatorError(
                f"disposable guest exited before {label} ({process.returncode})"
            )
    raise OperatorError(f"timed out waiting for {label}")


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


def _next_control_command(
    input_stream: TextIO,
    output: Callable[[str], None],
    warning: Callable[[str], None],
) -> tuple[str, bytes] | None:
    while True:
        output("rootkit> ")
        line = input_stream.readline()
        if not line:
            return None
        command = line.strip()
        if not command:
            continue
        if command.lower() in ("help", "?"):
            output(COMMAND_HELP)
            continue
        if command.lower() == "shell":
            command = f"SHELL {GUEST_SHELL_PORT}"
        elif command.lower() == "quit":
            command = "QUIT"
        elif command.lower().startswith("mark "):
            command = "MARK " + command[5:]
        else:
            command = command.upper()
        try:
            payload = _encode_control_command(command)
        except OperatorError as error:
            warning(str(error))
            continue
        return command, payload


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
    process: subprocess.Popen[str],
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


def run_reference_play(
    root: Path,
    module: Path,
    profile: str,
    script: Path,
    *,
    transcript: Callable[[list[object]], None],
    output: Callable[[str], None],
    success: Callable[[str], None],
    warning: Callable[[str], None],
    input_stream: TextIO | None = None,
) -> int:
    """Boot one restricted guest and operate its authenticated agent."""
    if profile not in ("authenticated-agent", "complete-rootkit"):
        raise OperatorError(f"unsupported play profile: {profile}")
    output_lock = threading.Lock()
    transcript = _serialized(transcript, output_lock)
    output = _serialized(output, output_lock)
    success = _serialized(success, output_lock)
    warning = _serialized(warning, output_lock)
    input_stream = input_stream or sys.stdin
    operator = HostOperator.create()
    helper_directory = tempfile.TemporaryDirectory(
        prefix="rootkitlings-operator-"
    )
    helper_root = Path(helper_directory.name)
    process = None
    pump = None
    peer = None
    secure = None
    guest_ready = threading.Event()
    enable_done = threading.Event()
    try:
        key_file = helper_root / "control.key"
        key_file.write_text(operator.key.hex() + "\n", encoding="ascii")
        key_file.chmod(0o600)
        command = [
            str(root / "vm/boot"),
            "--play", str(script),
            "--play-profile", profile,
            "--play-key-file", str(key_file),
            "--play-control-port", str(operator.control_port),
            "--play-shell-port", str(operator.shell_port),
            str(module),
        ]
        transcript(command)
        helper = _build_crypto_helper(
            root,
            helper_root,
            transcript,
            output,
        )
        process = subprocess.Popen(
            command,
            cwd=root,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=True,
            bufsize=1,
        )

        def forward_guest_output() -> None:
            if process is not None and process.stdout is not None:
                for line in process.stdout:
                    marker = line.rstrip("\r\n")
                    if marker == PLAY_READY:
                        guest_ready.set()
                        continue
                    if marker == PLAY_ENABLE_DONE:
                        enable_done.set()
                        continue
                    output(line)

        pump = threading.Thread(target=forward_guest_output, daemon=True)
        pump.start()
        peer = _accept_while_running(operator.control_listener, process, 90)
        peer.settimeout(30)
        _wait_for_guest_event(
            guest_ready,
            process,
            30,
            "play setup",
        )
        secure = SecureControl(
            operator.key,
            lambda key: OpenSslAead(
                key,
                helper,
                transcript,
                output,
            ),
        )
        session = secure.accept_hello(receive_frame(peer))
        success(f"Authenticated HELLO session={session}")
        output(
            "Commands: help, status, enable, disable, run, mark TEXT, "
            "shell, shell status, ping, info, quit\n"
        )
        output("The shell is a raw root /bin/sh stream without a PTY.\n")

        def exchange(command: str) -> str:
            output(f"> {command}\n")
            payload = _encode_control_command(command)
            send_frame(peer, secure.seal_request(payload))
            reply = secure.open_reply(receive_frame(peer)).decode(
                "ascii",
                errors="replace",
            )
            output(f"< {reply}\n")
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
            output(f"> {command_text}\n")
            send_frame(peer, secure.seal_request(payload))
            if command_text == "QUIT":
                break
            reply = secure.open_reply(receive_frame(peer)).decode(
                "ascii", errors="replace"
            )
            output(f"< {reply}\n")
            if (
                profile == "complete-rootkit"
                and command_text == "ENABLE"
                and reply == "ENABLED"
            ):
                _wait_for_guest_event(
                    enable_done,
                    process,
                    15,
                    "the enabled demonstration",
                )
            if reply.startswith("OPEN "):
                request_id, cookie = parse_shell_ticket(reply)
                shell = _accept_shell_while_running(
                    operator.shell_listener,
                    process,
                    request_id,
                    cookie,
                    30,
                    warning,
                )
                output(
                    "Root shell open. Type exit, then press Ctrl-D "
                    "to return.\n"
                )
                with shell:
                    _relay_shell(shell, input_stream, output)
                _poll_shell_status(request_id, exchange)
        return 0
    except KeyboardInterrupt as error:
        raise OperatorError("play session interrupted") from error
    except (OSError, OperatorError, UnicodeError) as error:
        raise OperatorError(str(error)) from error
    finally:
        if secure is not None:
            secure.close()
        if peer is not None:
            peer.close()
        operator.close()
        helper_directory.cleanup()
        if process is not None:
            _stop_process(process)
        if pump is not None:
            pump.join(timeout=2)


def _relay_shell(
    shell: socket.socket,
    input_stream: TextIO,
    output: Callable[[str], None],
) -> None:
    """Relay the raw non-PTY shell until EOF; Ctrl-D closes its input."""
    selector = selectors.DefaultSelector()
    selector.register(shell, selectors.EVENT_READ, "shell")
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
                else:
                    line = input_stream.readline()
                    if not line:
                        selector.unregister(input_stream)
                        shell.shutdown(socket.SHUT_WR)
                    else:
                        shell.sendall(line.encode())
    finally:
        selector.close()
