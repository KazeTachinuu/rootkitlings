#!/bin/bash
set -e

MODULE="hide"
KO="src/${MODULE}.ko"

if [ ! -f "$KO" ]; then
    echo "FAIL: $KO not found. Run make first."
    exit 1
fi

echo "==> Loading $MODULE"
insmod "$KO"

echo "==> Checking lsmod for '$MODULE'"
if lsmod | grep -q "$MODULE"; then
    echo "FAIL: module still visible in lsmod"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

echo "PASS: module hidden from lsmod"

echo "==> Attempting rmmod (might fail, that's expected for 026)"
rmmod "$MODULE" 2>/dev/null || echo "NOTE: rmmod failed (module unlinked from list). See exercise 028."

echo "==> dmesg tail:"
dmesg | tail -5
