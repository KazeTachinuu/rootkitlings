#!/bin/bash
set -e

echo "[042] Test: kernel TCP socket"

# Check no TODO remains
if grep -q "TODO(rootkitlings)" src/ksock.c; then
    echo "FAIL: src/ksock.c still has TODO markers"
    exit 1
fi

# Start a listener
nc -l -p 4444 > /tmp/042_nc_out &
NC_PID=$!
sleep 0.5

# Load module (should connect to nc)
insmod src/ksock.ko
sleep 1

# Check connection happened (nc should have exited or still running with connection)
if dmesg | tail -5 | grep -q "042: connected"; then
    echo "ok: module connected to server"
else
    echo "FAIL: module did not connect"
    dmesg | tail -10
    kill $NC_PID 2>/dev/null || true
    rmmod ksock 2>/dev/null || true
    exit 1
fi

# Cleanup
kill $NC_PID 2>/dev/null || true
rmmod ksock 2>/dev/null || true
rm -f /tmp/042_nc_out

echo "PASS: kernel socket created and connected"
