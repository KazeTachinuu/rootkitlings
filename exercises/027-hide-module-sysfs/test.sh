#!/bin/bash
set -e

MODULE="hidesys"
KO="src/${MODULE}.ko"

if [ ! -f "$KO" ]; then
    echo "FAIL: $KO not found. Run make first."
    exit 1
fi

echo "==> Loading $MODULE"
insmod "$KO"

echo "==> Checking lsmod"
if lsmod | grep -q "$MODULE"; then
    echo "FAIL: module still visible in lsmod"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi
echo "PASS: hidden from lsmod"

echo "==> Checking /sys/module/"
if [ -d "/sys/module/$MODULE" ]; then
    echo "FAIL: module still visible in /sys/module/"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi
echo "PASS: hidden from /sys/module/"

echo "==> Attempting rmmod (might fail)"
rmmod "$MODULE" 2>/dev/null || echo "NOTE: rmmod failed (expected). See exercise 028."

echo "==> dmesg tail:"
dmesg | tail -5
