#!/bin/bash
set -e

MODULE="hidefiles"
KO="src/${MODULE}.ko"

if [ ! -f "$KO" ]; then
    echo "FAIL: $KO not found. Run make first."
    exit 1
fi

echo "==> Creating test files"
touch /tmp/rootkit_secret
touch /tmp/rootkit_config
touch /tmp/normal_file

echo "==> Listing /tmp BEFORE hook"
ls /tmp/ | grep -E "(rootkit_|normal_)" || true

echo "==> Loading $MODULE"
insmod "$KO"

echo "==> Listing /tmp AFTER hook"
LISTING=$(ls /tmp/)

if echo "$LISTING" | grep -q "rootkit_secret"; then
    echo "FAIL: rootkit_secret still visible"
    rmmod "$MODULE"
    exit 1
fi

if echo "$LISTING" | grep -q "rootkit_config"; then
    echo "FAIL: rootkit_config still visible"
    rmmod "$MODULE"
    exit 1
fi

if ! echo "$LISTING" | grep -q "normal_file"; then
    echo "FAIL: normal_file was hidden (should not be)"
    rmmod "$MODULE"
    exit 1
fi

echo "PASS: rootkit_ files hidden, normal files visible"

echo "==> Unloading"
rmmod "$MODULE"

echo "==> Cleanup"
rm -f /tmp/rootkit_secret /tmp/rootkit_config /tmp/normal_file

echo "==> dmesg tail:"
dmesg | tail -5
