#!/usr/bin/env python3
"""C2 test server for exercise 045: speaks the TLV protocol."""
import socket
import struct
import sys

HOST = "127.0.0.1"
PORT = 4444

C2_HEARTBEAT = 0x01
C2_CMD_REQUEST = 0x02

def recv_exact(conn, n):
    data = b""
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            return data
        data += chunk
    return data

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((HOST, PORT))
srv.listen(1)
srv.settimeout(10)

print(f"[c2server] listening on {HOST}:{PORT}")
try:
    conn, addr = srv.accept()
    print(f"[c2server] connection from {addr}")

    # Receive heartbeat
    header = recv_exact(conn, 5)
    if len(header) == 5:
        msg_type = header[0]
        msg_len = struct.unpack("!I", header[1:5])[0]
        print(f"[c2server] received: type={msg_type} length={msg_len}")
        if msg_type == C2_HEARTBEAT:
            print("[c2server] heartbeat received")

    # Send command: "id"
    cmd = b"id"
    packet = struct.pack("!BI", C2_CMD_REQUEST, len(cmd)) + cmd
    conn.sendall(packet)
    print(f"[c2server] sent CMD_REQUEST: {cmd.decode()}")

    conn.close()
except socket.timeout:
    print("[c2server] timeout")
    sys.exit(1)
finally:
    srv.close()
