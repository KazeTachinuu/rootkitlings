"""WebSocket dashboard server for the operator console."""

from __future__ import annotations

import asyncio
import json
import threading
import time
from pathlib import Path
from typing import TYPE_CHECKING, Callable

from aiohttp import web

if TYPE_CHECKING:
    from engine.server import Target, TargetManager

HTTP_PORT = 8080

_manager: TargetManager | None = None
_ws_clients: set[web.WebSocketResponse] = set()
_event_log: list[dict] = []
_loop: asyncio.AbstractEventLoop | None = None


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

    return {
        "id": t.id,
        "exercise": t.exercise,
        "hostname": t.hostname,
        "kernel": t.kernel,
        "arch": t.arch,
        "online": t.online,
        "module_hidden": status.get("hidden", "?") == "yes",
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


async def _ws_endpoint(request: web.Request) -> web.WebSocketResponse:
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    _ws_clients.add(ws)
    try:
        await ws.send_str(json.dumps(_snapshot()))
        async for msg in ws:
            if msg.type != web.WSMsgType.TEXT:
                break
            try:
                data = json.loads(msg.data)
            except json.JSONDecodeError:
                continue

            cmd_type = data.get("type", "")

            if cmd_type == "refresh":
                await ws.send_str(json.dumps(_snapshot()))

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
                    reply = target.exchange(command)
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
    finally:
        _ws_clients.discard(ws)
    return ws


def _run_server(http_port: int, static_root: Path) -> None:
    """Single event loop: aiohttp serves static files + WS on one port."""
    global _loop

    async def run():
        global _loop
        _loop = asyncio.get_running_loop()

        app = web.Application()
        app.router.add_get("/ws", _ws_endpoint)
        app.router.add_static("/assets", static_root / "assets")
        app.router.add_static("/", static_root, show_index=False)

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
