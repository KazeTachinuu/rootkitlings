#!/usr/bin/env python3
# Minimal WebSocket client for operator check scripts.
# Zero dependencies beyond Python 3 stdlib.
# Usage: rkctl.py <ws-url> <action> [args...]
#   wait                  - block until an agent is online
#   cmd "PING"            - send command, print reply
#   download /path        - download file, write binary to stdout
#   download-size /path   - print file size in bytes
#   upload /path          - read stdin, upload to path
#   connections           - print connect count

import base64
import hashlib
import json
import os
import socket
import struct
import sys
import time
from urllib.parse import urlparse


def _ws_connect(url):
    u = urlparse(url)
    host = u.hostname or "127.0.0.1"
    port = u.port or 80
    path = u.path or "/ws"

    sock = socket.create_connection((host, port), timeout=5)
    key = base64.b64encode(os.urandom(16)).decode()
    sock.sendall(
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        f"Upgrade: websocket\r\n"
        f"Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        f"Sec-WebSocket-Version: 13\r\n"
        f"\r\n".encode()
    )
    resp = b""
    while b"\r\n\r\n" not in resp:
        resp += sock.recv(4096)
    if b"101" not in resp.split(b"\r\n")[0]:
        raise ConnectionError(resp.split(b"\r\n")[0].decode())
    return sock


def _ws_send(sock, text):
    payload = text.encode()
    mask = os.urandom(4)
    header = bytearray([0x81])
    length = len(payload)
    if length < 126:
        header.append(0x80 | length)
    elif length < 65536:
        header.append(0x80 | 126)
        header += struct.pack(">H", length)
    else:
        header.append(0x80 | 127)
        header += struct.pack(">Q", length)
    header += mask
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    sock.sendall(header + masked)


def _recv_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("connection closed")
        buf += chunk
    return bytes(buf)


def _ws_recv(sock):
    hdr = _recv_exact(sock, 2)
    opcode = hdr[0] & 0x0F
    masked = bool(hdr[1] & 0x80)
    length = hdr[1] & 0x7F
    if length == 126:
        length = struct.unpack(">H", _recv_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack(">Q", _recv_exact(sock, 8))[0]
    mask = _recv_exact(sock, 4) if masked else None
    data = _recv_exact(sock, length)
    if masked:
        data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    if opcode == 0x08:
        raise ConnectionError("server closed")
    if opcode == 0x09:
        sock.sendall(bytearray([0x8A, 0x80]) + os.urandom(4))
        return _ws_recv(sock)
    return data.decode()


def _first_target(state):
    for t in state.get("targets", []):
        if t.get("online", False):
            return t["id"]
    return None


def _send_and_wait(sock, msg, reply_type):
    _ws_send(sock, json.dumps(msg))
    while True:
        data = json.loads(_ws_recv(sock))
        if data.get("type") == reply_type:
            return data
        if data.get("type") == "event":
            continue


def main():
    if len(sys.argv) < 3:
        print("usage: rkctl.py <ws-url> <action> [args...]", file=sys.stderr)
        sys.exit(2)

    ws_url, action = sys.argv[1], sys.argv[2]
    args = sys.argv[3:]

    if action == "wait":
        for _ in range(30):
            try:
                sock = _ws_connect(ws_url)
                state = json.loads(_ws_recv(sock))
                if _first_target(state):
                    sock.close()
                    print("CONNECTED")
                    return
                sock.close()
            except Exception:
                pass
            time.sleep(1)
        print("TIMEOUT", file=sys.stderr)
        sys.exit(1)

    sock = _ws_connect(ws_url)
    state = json.loads(_ws_recv(sock))
    target = _first_target(state)
    if not target:
        print("no agent connected", file=sys.stderr)
        sys.exit(1)

    if action == "connections":
        print(state.get("connect_count", 0))
        sock.close()
        return

    if action == "cmd":
        result = _send_and_wait(
            sock,
            {"type": "command", "target": target, "command": args[0]},
            "result",
        )
        print(result.get("reply", ""))

    elif action == "download":
        result = _send_and_wait(
            sock,
            {"type": "download", "target": target, "path": args[0], "_dlId": 1},
            "download_result",
        )
        if result.get("ok") and result.get("data"):
            sys.stdout.buffer.write(base64.b64decode(result["data"]))
        else:
            print(result.get("error", "download failed"), file=sys.stderr)
            sys.exit(1)

    elif action == "download-size":
        result = _send_and_wait(
            sock,
            {"type": "download", "target": target, "path": args[0], "_dlId": 1},
            "download_result",
        )
        if result.get("ok"):
            print(result.get("size", 0))
        else:
            print(result.get("error", "download failed"), file=sys.stderr)
            sys.exit(1)

    elif action == "upload":
        data = sys.stdin.buffer.read()
        result = _send_and_wait(
            sock,
            {
                "type": "upload",
                "target": target,
                "path": args[0],
                "data": base64.b64encode(data).decode(),
                "_uploadId": 1,
            },
            "upload_result",
        )
        if result.get("ok"):
            print(result.get("reply", "OK"))
        else:
            print(result.get("error", "upload failed"), file=sys.stderr)
            sys.exit(1)

    else:
        print(f"unknown action: {action}", file=sys.stderr)
        sys.exit(2)

    sock.close()


if __name__ == "__main__":
    main()
