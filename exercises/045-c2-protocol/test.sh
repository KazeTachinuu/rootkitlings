#!/bin/bash
set -e

echo "[045] Test: C2 protocol"

if grep -q "TODO(rootkitlings)" src/c2proto.c; then
    echo "FAIL: src/c2proto.c still has TODO markers"
    exit 1
fi

# Start C2 server
python3 test_c2server.py &
SRV_PID=$!
sleep 1

# Load module
insmod src/c2proto.ko
sleep 2

wait $SRV_PID 2>/dev/null || true

# Check heartbeat was sent
if dmesg | tail -15 | grep -q "045: sent heartbeat"; then
    echo "ok: heartbeat sent"
else
    echo "FAIL: heartbeat not sent"
    dmesg | tail -15
    rmmod c2proto 2>/dev/null || true
    exit 1
fi

# Check command was received and parsed
if dmesg | tail -15 | grep -q "045: command received: id"; then
    echo "ok: command 'id' parsed from protocol"
else
    echo "FAIL: command not parsed"
    dmesg | tail -15
    rmmod c2proto 2>/dev/null || true
    exit 1
fi

rmmod c2proto 2>/dev/null || true

echo "PASS: C2 protocol works"
