"""Multi-target operator server with multiplexed TUI."""

from __future__ import annotations

import cmd
import os
import socket
import socketserver
import sys
import threading
import time
from pathlib import Path
from typing import Callable, TextIO

import lib.operator as _config
from lib.operator.control_console import (
    COMMAND_HELP,

    HostOperator,
    OpenSslAead,
    OperatorError,
    SecureControl,
    TargetConsole,
    TargetProcess,
    TargetSession,
    accept_shell_while_running,
    build_crypto_helper,
    encode_control_command,
    mark_operator_ready,
    poll_shell_status,
    relay_shell,
    parse_shell_ticket,
    read_target_session,
    receive_frame,
    send_frame,
)
from lib.operator.dashboard import add_event


class Target:
    """One connected target VM."""

    def __init__(
        self,
        target_id: str,
        label: str,
        session: TargetSession,
        operator: HostOperator,
        peer,
        secure: SecureControl,
        process: TargetProcess,
    ):
        self.id = target_id
        self.label = label
        self.session = session
        self.operator = operator
        self.peer = peer
        self.secure = secure
        self.process = process
        self.hostname = ""
        self.kernel = ""
        self.arch = ""
        self.build_id = ""
        self.online = True
        self._generation = 0
        self._lock = threading.Lock()

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

    def download_file(self, path: str,
                      on_progress: Callable[[int, int], None] | None = None,
                      ) -> tuple[bytes, int]:
        """Download a file from the target via the control channel.

        Returns (file_bytes, total_size).
        """
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
                    raw = self.secure.open_reply(receive_frame(self.peer))
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
                shell_port = self.operator.shell_port
                payload = encode_control_command(f"{command} {shell_port}")
                send_frame(self.peer, self.secure.seal_request(payload))
                reply = self.secure.open_reply(
                    receive_frame(self.peer)
                ).decode("ascii", errors="replace")
            except Exception:
                self.online = False
                raise
            request_id, cookie = parse_shell_ticket(reply)
            return accept_shell_while_running(
                self.operator.shell_listener,
                self.process,
                request_id,
                cookie,
                timeout,
                lambda msg: None,
            )

    def open_shell(self, timeout: float = 10) -> socket.socket:
        return self._request_shell("SHELL", timeout)

    def open_exec_shell(self, timeout: float = 10) -> socket.socket:
        return self._request_shell("EXEC_SHELL", timeout)

    def exec_shell(self, command: str, timeout: float = 5) -> str:
        start_marker = "__RK_START__"
        end_marker = "__RK_EOF__"
        shell = self.open_exec_shell(timeout)
        try:
            shell.settimeout(timeout)
            shell.recv(4096)
            wire = f"echo {start_marker}\n{command}\necho {end_marker}\n"
            shell.sendall(wire.encode())
            chunks = []
            marker = end_marker.encode()
            while True:
                chunk = shell.recv(4096)
                if not chunk:
                    break
                chunks.append(chunk)
                if marker in chunk:
                    break
            shell.sendall(b"exit\n")
        finally:
            try:
                shell.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            shell.close()
        text = b"".join(chunks).decode("utf-8", errors="replace")
        start = text.find(start_marker)
        if start >= 0:
            text = text[start + len(start_marker):]
        end = text.find(end_marker)
        if end >= 0:
            text = text[:end]
        return text.strip()

    def close(self) -> None:
        self.online = False
        for resource in (self.peer, self.secure, self.operator):
            try:
                resource.close()
            except Exception:
                pass


class TargetManager:
    """Thread-safe target registry."""

    def __init__(self):
        self._targets: dict[str, Target] = {}
        self._next_id = 1
        self._lock = threading.Lock()

    def add(
        self,
        label: str,
        session: TargetSession,
        operator: HostOperator,
        peer,
        secure: SecureControl,
        process: TargetProcess,
    ) -> tuple[Target, bool]:
        """Register a target. Returns (target, is_reconnect)."""
        with self._lock:
            port = operator.control_port
            for existing in self._targets.values():
                if existing.operator.control_port == port and not existing.online:
                    existing.reconnect(peer, secure)
                    return existing, True
            target_id = f"target-{self._next_id}"
            self._next_id += 1
            target = Target(
                target_id, label, session, operator, peer, secure, process,
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


class ServerConsole(cmd.Cmd):
    """Multiplexed TUI for managing multiple targets."""

    prompt = "server> "
    intro = None

    def __init__(
        self,
        manager: TargetManager,
        root: Path,
        output: Callable[[str], None],
        warning: Callable[[str], None],
        success: Callable[[str], None],
    ):
        super().__init__()
        self.manager = manager
        self.root = root
        self._output = output
        self._warning = warning
        self._success = success

    def do_targets(self, arg: str) -> None:
        """List all connected targets."""
        targets = self.manager.list_all()
        if not targets:
            self._output("  (no targets connected)\n")
            return
        for t in targets:
            status = "online" if t.online else "offline"
            info = ""
            if t.hostname:
                info = f"  {t.hostname} {t.kernel} {t.arch}"
            peer_addr = ""
            try:
                addr, port = t.peer.getpeername()
                peer_addr = f"  {addr}:{port}"
            except Exception:
                pass
            self._output(
                f"  {t.id:<12} {t.label:<6} {status:<8}{peer_addr}{info}\n"
            )

    def do_use(self, arg: str) -> None:
        """Switch to a target's operator console."""
        target_id = arg.strip()
        if not target_id:
            self._warning("usage: use <target-id>\n")
            return
        if target_id.isdigit():
            target_id = f"target-{target_id}"
        target = self.manager.get(target_id)
        if not target:
            self._warning(f"unknown target: {target_id}\n")
            return
        if not target.online:
            self._warning(f"{target_id} is offline\n")
            return
        self._success(f"switched to {target_id}")
        _run_target_console(target, self._output, self._warning, self._success)
        self._output(f"back to server view\n")

    def complete_use(self, text: str, line: str, begidx: int, endidx: int):
        targets = self.manager.list_all()
        ids = [t.id for t in targets if t.online]
        return [i for i in ids if i.startswith(text)]

    def do_kick(self, arg: str) -> None:
        """Disconnect and remove a target."""
        target_id = arg.strip()
        if not target_id:
            self._warning("usage: kick <target-id>\n")
            return
        if target_id.isdigit():
            target_id = f"target-{target_id}"
        target = self.manager.remove(target_id)
        if target:
            self._success(f"removed {target_id}")
        else:
            self._warning(f"unknown target: {target_id}\n")

    def complete_kick(self, text: str, line: str, begidx: int, endidx: int):
        targets = self.manager.list_all()
        ids = [t.id for t in targets]
        return [i for i in ids if i.startswith(text)]

    def do_info(self, arg: str) -> None:
        """Server summary."""
        targets = self.manager.list_all()
        online = sum(1 for t in targets if t.online)
        self._output(f"  targets: {len(targets)} ({online} online)\n")

    def do_quit(self, arg: str) -> bool:
        return True

    do_EOF = do_quit

    def do_help(self, arg: str) -> None:
        self._output(
            "Server\n"
            "  targets             list connected targets\n"
            "  use ID              switch to target (use 1 or use target-1)\n"
            "  kick ID             disconnect and remove a target\n"
            "  info                target count and status\n"
            "  quit                stop the server\n"
        )

    def default(self, line: str) -> None:
        self._warning(f"unknown command: {line}; type help\n")

    def emptyline(self) -> None:
        pass


def _run_target_console(
    target: Target,
    output: Callable[[str], None],
    warning: Callable[[str], None],
    success: Callable[[str], None],
) -> None:
    """Run an operator console for a single target."""

    def exchange(command: str) -> str:
        output(f"send    {command}\n")
        reply = exchange_quiet(command)
        if reply:
            output(f"recv    {reply}\n")
        return reply

    def exchange_quiet(command: str) -> str:
        try:
            return target.exchange(command)
        except (OSError, OperatorError) as error:
            target.online = False
            warning(f"  {target.id} disconnected: {error}\n")
            return ""

    def handle_shell(reply: str) -> None:
        request_id, cookie = parse_shell_ticket(reply)
        output("  waiting for shell callback...\n")
        shell = accept_shell_while_running(
            target.operator.shell_listener,
            target.process,
            request_id,
            cookie,
            30,
            lambda msg: warning(f"  {msg}\n"),
        )
        success("shell connected")
        output("  Remote root Bash. Type exit to return.\n")
        with shell:
            relay_shell(shell, sys.stdin, output)
        output("\n  Remote Bash closed.\n")
        poll_shell_status(request_id, exchange_quiet)

    console = TargetConsole(
        exchange, exchange_quiet, output, warning, success, handle_shell,
        target.operator.shell_port, target=target,
    )
    console.prompt = f"{target.id}> "
    try:
        console.cmdloop()
    except KeyboardInterrupt:
        output("\n")


def _watcher_loop(
    root: Path,
    manager: TargetManager,
    helper: Path,
    output: Callable[[str], None],
    success: Callable[[str], None],
    warning: Callable[[str], None],
) -> None:
    """Watch for new target sessions and accept connections."""
    run_dir = root / _config.state_dir / "run"
    known: dict[str, float] = {}

    while True:
        try:
            if not run_dir.is_dir():
                time.sleep(1)
                continue

            for gone in [n for n in known if not (run_dir / n).is_dir()]:
                del known[gone]

            for entry in run_dir.iterdir():
                if not entry.is_dir() or entry.is_symlink():
                    continue

                session_file = entry / "session.toml"
                key_file = entry / "control.key"

                if not session_file.exists() or not key_file.exists():
                    continue

                try:
                    mtime = session_file.stat().st_mtime
                except OSError:
                    continue
                if known.get(entry.name) == mtime:
                    continue

                known[entry.name] = mtime
                threading.Thread(
                    target=_handle_session,
                    args=(
                        root, entry.name, manager, helper,
                        output, success, warning,
                    ),
                    daemon=True,
                ).start()

            # Detect dead target processes
            for target in manager.list_all():
                if target.online and target.process.poll() is not None:
                    target.online = False
                    sys.stdout.write(f"\r\033[K")
                    warning(
                        f"{target.id} ({target.label}):"
                        " target process stopped\n"
                    )
                    add_event(
                        f"offline {target.id} ({target.label})",
                        "disconnect",
                    )
                    sys.stdout.write(ServerConsole.prompt)
                    sys.stdout.flush()

            time.sleep(0.5)
        except Exception:
            time.sleep(1)


class _SessionHandler(socketserver.BaseRequestHandler):
    """Handle one agent connection: authenticate or drop."""

    def handle(self):
        ctx = self.server.session_ctx
        peer = self.request
        peer.settimeout(30)

        try:
            secure = SecureControl(
                ctx["key"],
                lambda key: OpenSslAead(key, ctx["helper"], ctx["output"]),
            )
            session_id = secure.accept_hello(receive_frame(peer))
        except Exception as e:
            _server_print(
                ctx["warning"],
                f"reject   {ctx['label']}: {e}\n",
            )
            return

        target, is_reconnect = ctx["manager"].add(
            ctx["label"], ctx["session"], ctx["operator"],
            peer, secure, ctx["process"],
        )

        try:
            reply = target.exchange("INFO")
            if reply.startswith("INFO "):
                parts = reply[5:].split(None, 3)
                if len(parts) >= 3:
                    target.hostname, target.kernel, target.arch = parts[:3]
                    target.build_id = parts[3] if len(parts) > 3 else ""
        except Exception:
            pass

        info = f" [{target.hostname}]" if target.hostname else ""
        verb = "reconn" if is_reconnect else "connect"
        _server_print(
            ctx["success"],
            f"{verb}  {target.id}  {ctx['label']}{info}"
            f"  session={session_id:016x}",
        )
        add_event(
            f"{verb} {target.id} {ctx['label']}{info}",
            "reconnect" if is_reconnect else "connect",
        )

        # TCP keepalive so the OS detects dead connections
        peer.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, 5)
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, 2)
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, 3)

        my_gen = target._generation
        # Wait until the connection drops or a newer handler supersedes us.
        while target.online and target._generation == my_gen \
                and ctx["process"].poll() is None:
            time.sleep(1)
        if target._generation != my_gen:
            return
        _server_print(
            ctx["warning"],
            f"lost     {target.id} ({ctx['label']}):"
            " agent disconnected, waiting for reconnect",
        )
        add_event(
            f"offline {target.id} ({ctx['label']})", "disconnect",
        )


class _SessionServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, existing_socket, ctx):
        self.session_ctx = ctx
        # Skip bind/listen: reuse the socket HostOperator already bound
        socketserver.TCPServer.__init__(
            self, existing_socket.getsockname(), _SessionHandler,
            bind_and_activate=False,
        )
        self.socket = existing_socket


def _server_print(fn, msg):
    sys.stdout.write(f"\r\033[K")
    fn(msg)
    sys.stdout.write(ServerConsole.prompt)
    sys.stdout.flush()


def _handle_session(
    root: Path,
    label: str,
    manager: TargetManager,
    helper: Path,
    output: Callable[[str], None],
    success: Callable[[str], None],
    warning: Callable[[str], None],
) -> None:
    """Run a ThreadingTCPServer on the session's control port.

    Accepts any number of connections. Each one authenticates via HELLO
    handshake or gets dropped. Runs until the target process dies.
    """
    try:
        session = read_target_session(root, label)
        operator = HostOperator.from_session(session)
    except OperatorError as error:
        warning(f"skip     {label}: {error}\n")
        return

    mark_operator_ready(session)
    _server_print(
        output,
        f"  session  {label}"
        f"  control=:{operator.control_port}"
        f"  shell=:{operator.shell_port}\n",
    )

    process = TargetProcess(session)

    ctx = {
        "label": label,
        "session": session,
        "operator": operator,
        "process": process,
        "manager": manager,
        "helper": helper,
        "output": output,
        "success": success,
        "warning": warning,
        "key": operator.key,
    }

    server = _SessionServer(operator.control_listener, ctx)

    server_thread = threading.Thread(
        target=server.serve_forever, daemon=True,
    )
    server_thread.start()

    # Wait until the target process dies
    while process.poll() is None:
        time.sleep(1)

    server.shutdown()
    operator.close()


def server_running(root: Path) -> bool:
    """Check whether an operator server is alive."""
    pid_file = root / _config.state_dir / "server.pid"
    try:
        pid = int(pid_file.read_text().strip())
        os.kill(pid, 0)
        return True
    except (FileNotFoundError, ValueError, ProcessLookupError, PermissionError):
        return False


def run_server(
    root: Path,
    *,
    transcript: Callable[[list[object]], None],
    output: Callable[[str], None],
    success: Callable[[str], None],
    warning: Callable[[str], None],
    banner: str = "",
    webapp_dist: Path | None = None,
) -> int:
    """Start the multi-target operator server."""
    if server_running(root):
        raise OperatorError("another server is already running")

    if banner:
        output(banner)

    helper = build_crypto_helper(
        root, root / _config.state_dir / "tools", transcript, output,
    )

    manager = TargetManager()
    pid_file = root / _config.state_dir / "server.pid"
    pid_file.parent.mkdir(parents=True, exist_ok=True)
    pid_file.write_text(str(os.getpid()))

    try:
        threading.Thread(
            target=_watcher_loop,
            args=(root, manager, helper, output, success, warning),
            daemon=True,
        ).start()

        from lib.operator.dashboard import start_dashboard
        if webapp_dist is None:
            webapp_dist = root / "webapp" / "dist"
        if webapp_dist.is_dir():
            start_dashboard(manager, webapp_dist / "index.html", output)

        console = ServerConsole(manager, root, output, warning, success)
        console.cmdloop()
        return 0
    except KeyboardInterrupt:
        output("\n")
        return 0
    finally:
        manager.close_all()
        pid_file.unlink(missing_ok=True)
        output("Server stopped.\n")
