#!/usr/bin/env python3
"""Test server for exercise 044: receives hello, sends ack."""
import socket
import sys

HOST = "127.0.0.1"
PORT = 4444

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((HOST, PORT))
srv.listen(1)
srv.settimeout(10)

print(f"[server] listening on {HOST}:{PORT}")
try:
    conn, addr = srv.accept()
    print(f"[server] connection from {addr}")

    data = conn.recv(1024)
    print(f"[server] received: {data.decode().strip()}")

    conn.sendall(b"ack from server\n")
    print("[server] sent ack")

    conn.close()
except socket.timeout:
    print("[server] timeout, no connection received")
    sys.exit(1)
finally:
    srv.close()
