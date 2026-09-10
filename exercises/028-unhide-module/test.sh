#!/bin/bash
set -e

MODULE="unhide"
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

echo "==> Attempting rmmod (should succeed cleanly)"
if rmmod "$MODULE"; then
    echo "PASS: rmmod succeeded (module restored itself)"
else
    echo "FAIL: rmmod failed. The module_exit should re-link before unload."
    exit 1
fi

echo "==> Checking dmesg for restore message"
if dmesg | tail -5 | grep -q "restored to module list"; then
    echo "PASS: restore message found in dmesg"
else
    echo "WARN: restore message not found (check your pr_info)"
fi
