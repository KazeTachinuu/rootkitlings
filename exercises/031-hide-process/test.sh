#!/bin/bash
set -e

MODULE="hideproc"
KO="src/${MODULE}.ko"

if [ ! -f "$KO" ]; then
    echo "FAIL: $KO not found. Run make first."
    exit 1
fi

echo "==> Starting a background process to hide"
sleep 9999 &
TARGET_PID=$!
echo "    Target PID: $TARGET_PID"

echo "==> Verifying process exists BEFORE hook"
if ! ps aux | grep -v grep | grep "sleep 9999" > /dev/null; then
    echo "FAIL: background process not found"
    kill $TARGET_PID 2>/dev/null || true
    exit 1
fi
echo "    Process visible in ps: yes"

echo "==> Loading $MODULE with target_pid=$TARGET_PID"
insmod "$KO" target_pid=$TARGET_PID

echo "==> Checking ps AFTER hook"
if ps aux | grep -v grep | grep "sleep 9999" > /dev/null; then
    echo "FAIL: process still visible in ps"
    rmmod "$MODULE"
    kill $TARGET_PID 2>/dev/null || true
    exit 1
fi

echo "PASS: process hidden from ps"

echo "==> Process still running (check /proc/$TARGET_PID directly):"
ls -la /proc/$TARGET_PID/cmdline 2>/dev/null && echo "    /proc/$TARGET_PID accessible directly" || echo "    /proc/$TARGET_PID not accessible"

echo "==> Unloading"
rmmod "$MODULE"

echo "==> Process visible again:"
ps aux | grep -v grep | grep "sleep 9999" || echo "    (might need to re-read /proc)"

echo "==> Cleanup"
kill $TARGET_PID 2>/dev/null || true

echo "==> dmesg tail:"
dmesg | tail -5
