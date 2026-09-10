#!/usr/bin/env python3
"""Full C2 test server for exercise 047.
Waits for heartbeat, sends 'id' command, checks response contains uid=0."""
import socket
import struct
import sys

HOST = "127.0.0.1"
PORT = 4444

C2_HEARTBEAT = 0x01
C2_CMD_REQUEST = 0x02
C2_CMD_RESPONSE = 0x03
HEADER_SIZE = 5

def recv_exact(conn, n):
    data = b""
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            return data
        data += chunk
    return data

def recv_message(conn):
    header = recv_exact(conn, HEADER_SIZE)
    if len(header) < HEADER_SIZE:
        return None, None
    msg_type = header[0]
    msg_len = struct.unpack("!I", header[1:5])[0]
    data = b""
    if msg_len > 0:
        data = recv_exact(conn, msg_len)
    return msg_type, data

def send_message(conn, msg_type, data=b""):
    header = struct.pack("!BI", msg_type, len(data))
    conn.sendall(header + data)

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((HOST, PORT))
srv.listen(1)
srv.settimeout(15)

print(f"[c2server] listening on {HOST}:{PORT}")
success = False
try:
    conn, addr = srv.accept()
    print(f"[c2server] connection from {addr}")

    # Wait for heartbeat
    msg_type, data = recv_message(conn)
    if msg_type == C2_HEARTBEAT:
        print("[c2server] heartbeat received")
    else:
        print(f"[c2server] unexpected message type: {msg_type}")
        sys.exit(1)

    # Send command
    send_message(conn, C2_CMD_REQUEST, b"id")
    print("[c2server] sent command: id")

    # Wait for response
    msg_type, data = recv_message(conn)
    if msg_type == C2_CMD_RESPONSE:
        response = data.decode(errors="replace").strip()
        print(f"[c2server] response: {response}")
        if "uid=0" in response:
            print("[c2server] PASS: response contains uid=0")
            success = True
        else:
            print("[c2server] FAIL: response does not contain uid=0")
    else:
        print(f"[c2server] unexpected message type: {msg_type}")

    conn.close()
except socket.timeout:
    print("[c2server] timeout")
finally:
    srv.close()

sys.exit(0 if success else 1)
