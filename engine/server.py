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

from engine.control_console import (
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
from engine.dashboard import add_event


class Target:
    """One connected target VM."""

    def __init__(
        self,
        target_id: str,
        exercise: str,
        session: TargetSession,
        operator: HostOperator,
        peer,
        secure: SecureControl,
        process: TargetProcess,
    ):
        self.id = target_id
        self.exercise = exercise
        self.session = session
        self.operator = operator
        self.peer = peer
        self.secure = secure
        self.process = process
        self.hostname = ""
        self.kernel = ""
        self.arch = ""
        self.online = True
        self._lock = threading.Lock()

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
        exercise: str,
        session: TargetSession,
        operator: HostOperator,
        peer,
        secure: SecureControl,
        process: TargetProcess,
    ) -> Target:
        with self._lock:
            target_id = f"target-{self._next_id}"
            self._next_id += 1
            target = Target(
                target_id, exercise, session, operator, peer, secure, process,
            )
            self._targets[target_id] = target
            return target

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
            self._output(f"  {t.id:<12} {t.exercise:<6} {status}{info}\n")

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
        target.operator.shell_port,
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
    run_dir = root / ".rklings/run"
    known: set[str] = set()

    while True:
        try:
            if not run_dir.is_dir():
                time.sleep(1)
                continue

            # ponytail: purge stale entries so re-runs of the same exercise work
            known -= {name for name in known if not (run_dir / name).is_dir()}

            for entry in run_dir.iterdir():
                if not entry.is_dir() or entry.is_symlink():
                    continue
                if entry.name in known:
                    continue

                session_file = entry / "session.toml"
                key_file = entry / "control.key"

                if not session_file.exists() or not key_file.exists():
                    continue

                known.add(entry.name)
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
                        f"{target.id} ({target.exercise}):"
                        " target process stopped\n"
                    )
                    add_event(
                        f"offline {target.id} ({target.exercise})",
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
                f"reject   {ctx['exercise']}: {e}\n",
            )
            return

        target = ctx["manager"].add(
            ctx["exercise"], ctx["session"], ctx["operator"],
            peer, secure, ctx["process"],
        )

        try:
            reply = target.exchange("INFO")
            if reply.startswith("INFO "):
                parts = reply[5:].split(None, 2)
                if len(parts) >= 3:
                    target.hostname, target.kernel, target.arch = parts
        except Exception:
            pass

        info = f" [{target.hostname}]" if target.hostname else ""
        _server_print(
            ctx["success"],
            f"connect  {target.id}  {ctx['exercise']}{info}"
            f"  session={session_id:016x}",
        )
        add_event(
            f"connect {target.id} {ctx['exercise']}{info}", "connect",
        )

        # TCP keepalive so the OS detects dead connections
        peer.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, 5)
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, 2)
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, 3)

        # Wait until the connection drops. The socket is shared with
        # dashboard and console threads via target.exchange(), so we
        # must not read/peek from it here. A failed exchange() sets
        # target.online = False; we just watch that flag.
        while target.online and ctx["process"].poll() is None:
            time.sleep(1)
        _server_print(
            ctx["warning"],
            f"lost     {target.id} ({ctx['exercise']}):"
            " agent disconnected, waiting for reconnect",
        )
        add_event(
            f"offline {target.id} ({ctx['exercise']})", "disconnect",
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
    exercise: str,
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
        session = read_target_session(root, exercise)
        operator = HostOperator.from_session(session)
    except OperatorError as error:
        warning(f"skip     {exercise}: {error}\n")
        return

    mark_operator_ready(session)
    _server_print(
        output,
        f"  session  {exercise}"
        f"  control=:{operator.control_port}"
        f"  shell=:{operator.shell_port}\n",
    )

    process = TargetProcess(session)

    ctx = {
        "exercise": exercise,
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
    pid_file = root / ".rklings/server.pid"
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
) -> int:
    """Start the multi-target operator server."""
    if server_running(root):
        raise OperatorError("another server is already running")

    output(
        "Rklings operator server\n"
        "Watching for targets. Start one with:\n"
        "  ./rklings run <EXERCISE> --reference\n\n"
    )

    helper = build_crypto_helper(
        root, root / ".rklings/tools", transcript, output,
    )

    manager = TargetManager()
    pid_file = root / ".rklings/server.pid"
    pid_file.parent.mkdir(parents=True, exist_ok=True)
    pid_file.write_text(str(os.getpid()))

    try:
        threading.Thread(
            target=_watcher_loop,
            args=(root, manager, helper, output, success, warning),
            daemon=True,
        ).start()

        from engine.dashboard import start_dashboard
        webapp_dist = root / "engine" / "webapp" / "dist"
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
