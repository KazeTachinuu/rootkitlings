#!/usr/bin/env python3
"""Encrypted C2 test server for exercise 050.
Speaks TLV protocol with AES-CBC encrypted data fields.
Data format: [IV (16 bytes)] [ciphertext (padded to 16)]
"""
import socket
import struct
import sys
import os
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import padding as sym_padding

HOST = "127.0.0.1"
PORT = 4444
KEY = b"rootkitlingskey!"

C2_HEARTBEAT = 0x01
C2_CMD_REQUEST = 0x02
C2_CMD_RESPONSE = 0x03
HEADER_SIZE = 5
AES_BLOCK = 16

def aes_encrypt(plaintext, key):
    iv = os.urandom(AES_BLOCK)
    padder = sym_padding.PKCS7(128).padder()
    padded = padder.update(plaintext) + padder.finalize()
    cipher = Cipher(algorithms.AES(key), modes.CBC(iv))
    enc = cipher.encryptor()
    ct = enc.update(padded) + enc.finalize()
    return iv + ct

def aes_decrypt(data, key):
    iv = data[:AES_BLOCK]
    ct = data[AES_BLOCK:]
    cipher = Cipher(algorithms.AES(key), modes.CBC(iv))
    dec = cipher.decryptor()
    padded = dec.update(ct) + dec.finalize()
    unpadder = sym_padding.PKCS7(128).unpadder()
    return unpadder.update(padded) + unpadder.finalize()

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

print(f"[crypto_c2] listening on {HOST}:{PORT}")
success = False
try:
    conn, addr = srv.accept()
    print(f"[crypto_c2] connection from {addr}")

    # Wait for heartbeat
    msg_type, data = recv_message(conn)
    if msg_type == C2_HEARTBEAT:
        print("[crypto_c2] heartbeat received")
    else:
        print(f"[crypto_c2] unexpected type: {msg_type}")
        sys.exit(1)

    # Send encrypted command: "whoami"
    encrypted_cmd = aes_encrypt(b"whoami", KEY)
    send_message(conn, C2_CMD_REQUEST, encrypted_cmd)
    print(f"[crypto_c2] sent encrypted CMD_REQUEST: whoami ({len(encrypted_cmd)} bytes)")

    # Receive encrypted response
    msg_type, data = recv_message(conn)
    if msg_type == C2_CMD_RESPONSE and data:
        try:
            response = aes_decrypt(data, KEY).decode(errors="replace").strip()
            print(f"[crypto_c2] decrypted response: {response}")
            if "root" in response:
                print("[crypto_c2] PASS: response contains 'root'")
                success = True
            else:
                print(f"[crypto_c2] FAIL: expected 'root' in response")
        except Exception as e:
            print(f"[crypto_c2] FAIL: decryption error: {e}")
    else:
        print(f"[crypto_c2] FAIL: unexpected type {msg_type} or empty data")

    conn.close()
except socket.timeout:
    print("[crypto_c2] timeout")
finally:
    srv.close()

sys.exit(0 if success else 1)
