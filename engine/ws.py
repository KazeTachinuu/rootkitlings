"""Minimal RFC 6455 WebSocket server. Zero third-party dependencies."""

from __future__ import annotations

import asyncio
import base64
import hashlib
import struct
from typing import AsyncIterator

GUID = "258EAFA5-E914-47DA-95CA-5AB5C04A265D"

OP_TEXT = 0x1
OP_CLOSE = 0x8
OP_PING = 0x9
OP_PONG = 0xA

FRAME_LIMIT = 1_048_576


def _accept_key(client_key: str) -> str:
    digest = hashlib.sha1((client_key + GUID).encode()).digest()
    return base64.b64encode(digest).decode()


def _encode(opcode: int, payload: bytes) -> bytes:
    length = len(payload)
    if length < 126:
        header = struct.pack("!BB", 0x80 | opcode, length)
    elif length < 0x10000:
        header = struct.pack("!BBH", 0x80 | opcode, 126, length)
    else:
        header = struct.pack("!BBQ", 0x80 | opcode, 127, length)
    return header + payload


def _unmask(mask: bytes, data: bytearray) -> bytes:
    for i in range(len(data)):
        data[i] ^= mask[i & 3]
    return bytes(data)


class WebSocket:
    """A single WebSocket connection."""

    def __init__(self, reader: asyncio.StreamReader,
                 writer: asyncio.StreamWriter) -> None:
        self._r = reader
        self._w = writer
        self._closed = False

    async def send(self, text: str) -> None:
        if self._closed:
            return
        self._w.write(_encode(OP_TEXT, text.encode()))
        await self._w.drain()

    async def __aiter__(self) -> AsyncIterator[str]:
        while not self._closed:
            try:
                op, payload = await self._recv()
            except (ConnectionError, asyncio.IncompleteReadError, ValueError):
                break
            if op == OP_TEXT:
                yield payload.decode(errors="replace")
            elif op == OP_CLOSE:
                self._closed = True
                try:
                    self._w.write(_encode(OP_CLOSE, payload[:2]))
                    await self._w.drain()
                except Exception:
                    pass
                break
            elif op == OP_PING:
                self._w.write(_encode(OP_PONG, payload))
                await self._w.drain()

    async def _recv(self) -> tuple[int, bytes]:
        head = await self._r.readexactly(2)
        op = head[0] & 0x0F
        masked = head[1] & 0x80
        length = head[1] & 0x7F

        if length == 126:
            length = struct.unpack("!H", await self._r.readexactly(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", await self._r.readexactly(8))[0]
        if length > FRAME_LIMIT:
            raise ValueError("frame too large")

        if masked:
            mask = await self._r.readexactly(4)
            return op, _unmask(mask, bytearray(await self._r.readexactly(length)))
        return op, await self._r.readexactly(length)

    def close(self) -> None:
        self._closed = True
        self._w.close()


async def _handshake(reader: asyncio.StreamReader,
                     writer: asyncio.StreamWriter) -> bool:
    request = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout=5.0)

    key = None
    for line in request.decode(errors="replace").splitlines():
        if line.lower().startswith("sec-websocket-key:"):
            key = line.split(":", 1)[1].strip()
            break

    if key is None:
        writer.write(b"HTTP/1.1 400 Bad Request\r\n\r\n")
        await writer.drain()
        writer.close()
        return False

    response = (
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Accept: {_accept_key(key)}\r\n"
        "\r\n"
    )
    writer.write(response.encode())
    await writer.drain()
    return True


async def serve(handler, host: str, port: int) -> None:
    """Run a WebSocket server. Calls handler(ws) per connection."""

    async def on_connect(reader: asyncio.StreamReader,
                         writer: asyncio.StreamWriter) -> None:
        try:
            if not await _handshake(reader, writer):
                return
            ws = WebSocket(reader, writer)
            try:
                await handler(ws)
            finally:
                ws.close()
        except Exception:
            pass
        finally:
            writer.close()

    server = await asyncio.start_server(on_connect, host, port)
    async with server:
        await server.serve_forever()
