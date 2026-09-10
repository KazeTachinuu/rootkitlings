#!/bin/bash
set -e

MODULE="hidedir"
KO="src/${MODULE}.ko"

if [ ! -f "$KO" ]; then
    echo "FAIL: $KO not found. Run make first."
    exit 1
fi

echo "==> Creating test directory"
mkdir -p /tmp/.rootkit_lair
echo "secret" > /tmp/.rootkit_lair/plans.txt
mkdir -p /tmp/.normal_dir

echo "==> Listing /tmp BEFORE hook (with -la)"
ls -la /tmp/ | grep -E "(rootkit_lair|normal_dir)" || true

echo "==> Loading $MODULE"
insmod "$KO"

echo "==> Listing /tmp AFTER hook (with -la)"
LISTING=$(ls -la /tmp/)

if echo "$LISTING" | grep -q ".rootkit_lair"; then
    echo "FAIL: .rootkit_lair still visible"
    rmmod "$MODULE"
    exit 1
fi

if ! echo "$LISTING" | grep -q ".normal_dir"; then
    echo "FAIL: .normal_dir was hidden (should not be)"
    rmmod "$MODULE"
    exit 1
fi

echo "PASS: .rootkit_lair hidden, .normal_dir visible"

echo "==> The files inside are still accessible by direct path:"
cat /tmp/.rootkit_lair/plans.txt && echo "(file content accessible)" || echo "(inaccessible)"

echo "==> Unloading"
rmmod "$MODULE"

echo "==> Cleanup"
rm -rf /tmp/.rootkit_lair /tmp/.normal_dir

echo "==> dmesg tail:"
dmesg | tail -5
