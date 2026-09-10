#!/bin/bash
set -e

echo "[041] Test: self-reload via call_usermodehelper"

# Check no TODO remains
if grep -q "TODO(rootkitlings)" src/reload.c; then
    echo "FAIL: src/reload.c still has TODO markers"
    exit 1
fi

# Setup
cp src/reload.ko /tmp/reload.ko
cp src/reload.sh /tmp/reload.sh
chmod +x /tmp/reload.sh

# Load
insmod /tmp/reload.ko
if ! lsmod | grep -q "^reload"; then
    echo "FAIL: module did not load"
    exit 1
fi
echo "ok: module loaded"

# Unload
rmmod reload
echo "ok: module unloaded, waiting 3 seconds for self-reload..."
sleep 3

# Check if it came back
if lsmod | grep -q "^reload"; then
    echo "ok: module reloaded itself"
else
    echo "FAIL: module did not come back after rmmod"
    echo "      Check dmesg for errors"
    dmesg | tail -10
    exit 1
fi

# Final cleanup
rmmod reload 2>/dev/null || true
rm -f /tmp/reload.ko /tmp/reload.sh

echo "PASS: self-reload works"
