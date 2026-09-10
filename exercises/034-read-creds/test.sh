#!/bin/bash
set -e

MODULE="readcreds"
KO="src/${MODULE}.ko"

if [ ! -f "$KO" ]; then
    echo "FAIL: $KO not found. Run make first."
    exit 1
fi

echo "==> Loading $MODULE (as root, insmod requires it)"
insmod "$KO"

echo "==> Checking dmesg for credential output"
DMESG=$(dmesg | tail -10)

if echo "$DMESG" | grep -q "uid="; then
    echo "PASS: credentials printed in dmesg"
    echo "$DMESG" | grep "rootkitlings/034"
else
    echo "FAIL: no credential output in dmesg"
    rmmod "$MODULE"
    exit 1
fi

echo "==> Since insmod runs as root, uid should be 0"
if echo "$DMESG" | grep -q "uid=0"; then
    echo "PASS: uid=0 confirmed (insmod runs as root)"
else
    echo "WARN: uid not 0 (unexpected for insmod)"
fi

echo "==> Unloading"
rmmod "$MODULE"
