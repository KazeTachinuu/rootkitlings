#!/usr/bin/env python3
"""XOR test server for exercise 048."""
import socket
import sys

HOST = "127.0.0.1"
PORT = 4444
KEY = b"rootkitlings"

def xor_bytes(data, key):
    return bytes(b ^ key[i % len(key)] for i, b in enumerate(data))

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind((HOST, PORT))
srv.listen(1)
srv.settimeout(10)

print(f"[xor_server] listening on {HOST}:{PORT}")
success = False
try:
    conn, addr = srv.accept()
    print(f"[xor_server] connection from {addr}")

    # Send XOR-encrypted message
    plaintext = b"hello from server"
    encrypted = xor_bytes(plaintext, KEY)
    conn.sendall(encrypted)
    print(f"[xor_server] sent encrypted: {plaintext.decode()}")

    # Receive XOR-encrypted reply
    data = conn.recv(1024)
    if data:
        decrypted = xor_bytes(data, KEY)
        reply = decrypted.decode(errors="replace")
        print(f"[xor_server] received encrypted, decrypted: {reply}")
        if reply == "xor works":
            print("[xor_server] PASS: reply matches")
            success = True
        else:
            print(f"[xor_server] FAIL: expected 'xor works', got '{reply}'")
    else:
        print("[xor_server] FAIL: no data received")

    conn.close()
except socket.timeout:
    print("[xor_server] timeout")
finally:
    srv.close()

sys.exit(0 if success else 1)
