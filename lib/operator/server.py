"""Multi-target operator server."""

from __future__ import annotations

import logging
import os
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
    HostOperator,
    OperatorConsole,
    OpenSslAead,
    OperatorError,
    SecureControl,
    TargetProcess,
    TargetSession,
    accept_shell_while_running,
    build_crypto_helper,
    encode_control_command,
    mark_operator_ready,
    parse_shell_ticket,
    protocol_error,
    read_target_session,
    receive_frame,
    send_frame,
    setup_logging as _setup_logging,
)
from lib.operator.dashboard import add_event

log = logging.getLogger("operator")


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
        self._exec_shell = None
        self._exec_lock = threading.Lock()

    def reconnect(self, peer, secure, session=None, operator=None, process=None):
        with self._lock:
            try:
                self.peer.close()
            except Exception:
                pass
            self.peer = peer
            self.secure = secure
            if session is not None:
                self.session = session
            if operator is not None:
                self.operator = operator
            if process is not None:
                self.process = process
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
                chunk_num = 0
                while True:
                    frame = receive_frame(self.peer)
                    raw = self.secure.open_reply(frame)
                    if raw == b"EOF":
                        break
                    chunk_num += 1
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
            if reply.startswith("ERR"):
                msg = protocol_error(reply) or reply
                raise OperatorError(f"shell failed: {msg}")
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

    def _ensure_exec_shell(self, timeout: float = 10) -> socket.socket:
        """Return a persistent exec shell, opening one if needed."""
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
                # drain any leftover data from previous commands
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
            for existing in self._targets.values():
                if existing.label == label and not existing.online:
                    existing.reconnect(peer, secure, session, operator, process)
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


class _TargetCompleter(Completer):
    """Dynamic completer that lists live target IDs."""

    def __init__(self, manager: TargetManager, online_only: bool = False):
        self._manager = manager
        self._online_only = online_only

    def get_completions(self, document, complete_event):
        word = document.get_word_before_cursor()
        for t in self._manager.list_all():
            if self._online_only and not t.online:
                continue
            if t.id.startswith(word):
                yield Completion(t.id, start_position=-len(word))


class ServerConsole:
    """Multiplexed server console backed by prompt_toolkit + rich."""

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
    """Run an operator console for a single target."""

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
        shell = accept_shell_while_running(
            target.operator.shell_listener,
            target.process,
            request_id,
            cookie,
            30,
            lambda msg: log.warning("%s", msg),
        )
        log.info("shell connected")
        log.info("  Remote root Bash. Type exit to return.")
        with shell:
            from lib.operator.control_console import relay_shell
            relay_shell(shell, sys.stdin, lambda msg: sys.stdout.write(msg))
        log.info("Remote Bash closed.")

    console = OperatorConsole(
        exchange, exchange_quiet, handle_shell,
        shell_port=target.operator.shell_port,
        target=target,
    )
    console.prompt = f"{target.id}> "
    try:
        console.run()
    except KeyboardInterrupt:
        print()


def _watcher_loop(
    root: Path,
    manager: TargetManager,
    helper: Path,
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
                    args=(root, entry.name, manager, helper),
                    daemon=True,
                ).start()

            for target in manager.list_all():
                if target.online and target.process.poll() is not None:
                    target.online = False
                    log.warning(
                        "%s (%s): target process stopped",
                        target.id, target.label,
                    )
                    add_event(
                        f"offline {target.id} ({target.label})",
                        "disconnect",
                    )

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
                lambda key: OpenSslAead(
                    key, ctx["helper"],
                    lambda msg: log.debug("%s", msg.rstrip()),
                ),
            )
            session_id = secure.accept_hello(receive_frame(peer))
        except Exception as e:
            log.warning("reject   %s: %s", ctx["label"], e)
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
        log.info(
            "%s  %s  %s%s  session=%016x",
            verb, target.id, ctx["label"], info, session_id,
        )
        add_event(
            f"{verb} {target.id} {ctx['label']}{info}",
            "reconnect" if is_reconnect else "connect",
        )

        peer.settimeout(None)
        peer.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, 60)
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, 10)
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, 6)

        my_gen = target._generation
        import select
        while target.online and target._generation == my_gen \
                and ctx["process"].poll() is None:
            try:
                ready, _, _ = select.select([peer], [], [], 1.0)
                if ready:
                    chunk = peer.recv(1, socket.MSG_PEEK)
                    if not chunk:
                        break
            except (OSError, socket.error):
                break
        if target._generation != my_gen:
            return
        target.online = False
        log.warning(
            "lost     %s (%s): agent disconnected, waiting for reconnect",
            target.id, ctx["label"],
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


def _handle_session(
    root: Path,
    label: str,
    manager: TargetManager,
    helper: Path,
) -> None:
    """Run a ThreadingTCPServer on the session's control port.

    Accepts any number of connections. Each one authenticates via HELLO
    handshake or gets dropped. Runs until the target process dies.
    """
    try:
        session = read_target_session(root, label)
        operator = HostOperator.from_session(session)
    except OperatorError as error:
        log.warning("skip     %s: %s", label, error)
        return

    mark_operator_ready(session)
    log.info(
        "  session  %s  control=:%d  shell=:%d",
        label, operator.control_port, operator.shell_port,
    )

    process = TargetProcess(session)

    ctx = {
        "label": label,
        "session": session,
        "operator": operator,
        "process": process,
        "manager": manager,
        "helper": helper,
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
    banner: str = "",
    webapp_dist: Path | None = None,
    **_kwargs,
) -> int:
    """Start the multi-target operator server."""
    if server_running(root):
        raise OperatorError("another server is already running")

    _setup_logging()

    if banner:
        log.info("%s", banner.rstrip())

    helper = build_crypto_helper(
        root, root / _config.state_dir / "tools", transcript,
        lambda msg: log.debug("%s", msg.rstrip()),
    )

    manager = TargetManager()
    pid_file = root / _config.state_dir / "server.pid"
    pid_file.parent.mkdir(parents=True, exist_ok=True)
    pid_file.write_text(str(os.getpid()))

    try:
        threading.Thread(
            target=_watcher_loop,
            args=(root, manager, helper),
            daemon=True,
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
        pid_file.unlink(missing_ok=True)
        log.info("Server stopped.")
