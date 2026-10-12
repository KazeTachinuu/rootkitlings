"""WebSocket dashboard server for the operator console."""

from __future__ import annotations

import asyncio
import json
import mimetypes
import threading
import time
from pathlib import Path
from typing import TYPE_CHECKING, Callable

if TYPE_CHECKING:
    from engine.server import Target, TargetManager

WS_PORT = 8081
HTTP_PORT = 8080

_manager: TargetManager | None = None
_ws_clients: set = set()
_event_log: list[dict] = []


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
            asyncio.run_coroutine_threadsafe(ws.send(data), _loop)
        except Exception:
            _ws_clients.discard(ws)


_loop: asyncio.AbstractEventLoop | None = None


async def _ws_handler(ws):
    _ws_clients.add(ws)
    try:
        await ws.send(json.dumps(_snapshot()))

        async for raw in ws:
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                continue

            cmd_type = msg.get("type", "")

            if cmd_type == "refresh":
                await ws.send(json.dumps(_snapshot()))

            elif cmd_type == "command":
                target_id = msg.get("target")
                command = msg.get("command", "").strip()
                if not target_id or not command:
                    continue
                target = _manager.get(target_id) if _manager else None
                if not target or not target.online:
                    await ws.send(json.dumps({
                        "type": "result",
                        "target": target_id,
                        "command": command,
                        "ok": False,
                        "reply": "target offline",
                    }))
                    continue
                try:
                    reply = target.exchange(command)
                    await ws.send(json.dumps({
                        "type": "result",
                        "target": target_id,
                        "command": command,
                        "ok": True,
                        "reply": reply,
                    }))
                    add_event(f"{target.id}: {command}", "cmd")
                except Exception as e:
                    await ws.send(json.dumps({
                        "type": "result",
                        "target": target_id,
                        "command": command,
                        "ok": False,
                        "reply": str(e),
                    }))
    finally:
        _ws_clients.discard(ws)


async def _http_handler(reader: asyncio.StreamReader,
                        writer: asyncio.StreamWriter,
                        root: Path, index: str) -> None:
    """Serve static files over HTTP using asyncio streams."""
    try:
        request = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout=5.0)
    except (asyncio.TimeoutError, asyncio.IncompleteReadError):
        writer.close()
        return

    line = request.split(b"\r\n", 1)[0].decode(errors="replace")
    parts = line.split()
    if len(parts) < 2 or parts[0] != "GET":
        writer.write(b"HTTP/1.1 405 Method Not Allowed\r\n\r\n")
        await writer.drain()
        writer.close()
        return

    path = parts[1].split("?", 1)[0].split("#", 1)[0]
    if path == "/" or path == "/index.html":
        path = f"/{index}"

    resolved = (root / path.lstrip("/")).resolve()
    if not resolved.is_relative_to(root) or not resolved.is_file():
        writer.write(b"HTTP/1.1 404 Not Found\r\n\r\n")
        await writer.drain()
        writer.close()
        return

    body = resolved.read_bytes()
    content_type = mimetypes.guess_type(resolved.name)[0] or "application/octet-stream"
    header = (
        f"HTTP/1.1 200 OK\r\n"
        f"Content-Type: {content_type}\r\n"
        f"Content-Length: {len(body)}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    )
    writer.write(header.encode() + body)
    await writer.drain()
    writer.close()


def _run_servers(ws_port: int, http_port: int, html_path: Path) -> None:
    """Single event loop for both WebSocket and HTTP servers."""
    global _loop
    from engine.ws import serve as ws_serve

    root = html_path.parent.resolve()
    index = html_path.name

    async def run():
        global _loop
        _loop = asyncio.get_running_loop()

        http = await asyncio.start_server(
            lambda r, w: _http_handler(r, w, root, index),
            "0.0.0.0", http_port,
        )

        async with http:
            await asyncio.gather(
                http.serve_forever(),
                ws_serve(_ws_handler, "0.0.0.0", ws_port),
            )

    asyncio.run(run())


def start_dashboard(
    manager: TargetManager,
    html_path: Path,
    output: Callable[[str], None],
    *,
    ws_port: int = WS_PORT,
    http_port: int = HTTP_PORT,
) -> None:
    """Start the dashboard servers as a single daemon thread."""
    global _manager
    _manager = manager

    threading.Thread(
        target=_run_servers,
        args=(ws_port, http_port, html_path),
        daemon=True, name="dashboard",
    ).start()

    output(f"  dashboard: http://localhost:{http_port}\n")
    output(f"  websocket: ws://localhost:{ws_port}\n")
