#!/bin/bash
set -e

echo "[047] Test: full C2 loop"

if grep -q "TODO(rootkitlings)" src/c2loop.c; then
    echo "FAIL: src/c2loop.c still has TODO markers"
    exit 1
fi

# Start C2 server
python3 test_c2server.py &
SRV_PID=$!
sleep 1

# Load module
insmod src/c2loop.ko
sleep 5

# Wait for server
wait $SRV_PID
SRV_EXIT=$?

rmmod c2loop 2>/dev/null || true
rm -f /tmp/rkl_out

if [[ $SRV_EXIT -eq 0 ]]; then
    echo "PASS: full C2 loop works (heartbeat, command exec, response)"
else
    echo "FAIL: C2 server reported failure"
    dmesg | tail -15
    exit 1
fi
