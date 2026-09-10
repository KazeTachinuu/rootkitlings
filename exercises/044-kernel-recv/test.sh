#!/bin/bash
set -e

echo "[044] Test: kernel_recvmsg"

if grep -q "TODO(rootkitlings)" src/krecv.c; then
    echo "FAIL: src/krecv.c still has TODO markers"
    exit 1
fi

# Start test server
python3 test_server.py &
SRV_PID=$!
sleep 1

# Load module
insmod src/krecv.ko
sleep 2

# Wait for server to finish
wait $SRV_PID 2>/dev/null || true

# Check dmesg for received message
if dmesg | tail -10 | grep -q "044: received: ack from server"; then
    echo "ok: module received server response"
else
    echo "FAIL: module did not receive 'ack from server'"
    dmesg | tail -10
    rmmod krecv 2>/dev/null || true
    exit 1
fi

rmmod krecv 2>/dev/null || true

echo "PASS: kernel_recvmsg works"
