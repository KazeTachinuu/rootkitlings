#!/bin/bash
set -e

echo "[043] Test: kernel_sendmsg"

if grep -q "TODO(rootkitlings)" src/ksend.c; then
    echo "FAIL: src/ksend.c still has TODO markers"
    exit 1
fi

# Start listener, capture output
nc -l -p 4444 > /tmp/043_recv &
NC_PID=$!
sleep 0.5

# Load module
insmod src/ksend.ko
sleep 1

# Kill nc (it should have received data and the connection closed)
kill $NC_PID 2>/dev/null || true
wait $NC_PID 2>/dev/null || true

# Check received data
if grep -q "rootkitlings hello" /tmp/043_recv; then
    echo "ok: server received 'rootkitlings hello'"
else
    echo "FAIL: server did not receive expected message"
    echo "      Got: $(cat /tmp/043_recv)"
    rmmod ksend 2>/dev/null || true
    rm -f /tmp/043_recv
    exit 1
fi

rmmod ksend 2>/dev/null || true
rm -f /tmp/043_recv

echo "PASS: kernel_sendmsg works"
