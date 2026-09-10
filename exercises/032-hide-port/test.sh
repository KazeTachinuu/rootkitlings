#!/bin/bash
set -e

MODULE="hideport"
KO="src/${MODULE}.ko"
PORT=4444
HEX_PORT=$(printf '%04X' $PORT)

if [ ! -f "$KO" ]; then
    echo "FAIL: $KO not found. Run make first."
    exit 1
fi

echo "==> Starting listener on port $PORT (hex: $HEX_PORT)"
nc -l -p $PORT &
NC_PID=$!
sleep 1

echo "==> Checking /proc/net/tcp BEFORE hook"
if ! cat /proc/net/tcp | grep -qi "$HEX_PORT"; then
    echo "WARN: port $PORT not visible in /proc/net/tcp (listener may not have started)"
fi

echo "==> Loading $MODULE with target_port=$PORT"
insmod "$KO" target_port=$PORT

echo "==> Checking /proc/net/tcp AFTER hook"
if cat /proc/net/tcp | grep -qi ":${HEX_PORT}"; then
    echo "FAIL: port $PORT still visible in /proc/net/tcp"
    rmmod "$MODULE"
    kill $NC_PID 2>/dev/null || true
    exit 1
fi

echo "PASS: port $PORT hidden from /proc/net/tcp"

echo "==> Checking netstat"
if command -v netstat > /dev/null 2>&1; then
    if netstat -tlnp 2>/dev/null | grep -q ":${PORT}"; then
        echo "FAIL: port still visible in netstat"
    else
        echo "PASS: port hidden from netstat too"
    fi
fi

echo "==> Unloading"
rmmod "$MODULE"

echo "==> Cleanup"
kill $NC_PID 2>/dev/null || true

echo "==> dmesg tail:"
dmesg | tail -5
