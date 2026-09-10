#!/bin/bash
set -e

echo "[046] Test: exec command via call_usermodehelper"

if grep -q "TODO(rootkitlings)" src/execmd.c; then
    echo "FAIL: src/execmd.c still has TODO markers"
    exit 1
fi

# Cleanup
rm -f /tmp/rkl_out

# Load
insmod src/execmd.ko

# Check proc entry
if [[ ! -e /proc/rootkitlings_cmd ]]; then
    echo "FAIL: /proc/rootkitlings_cmd not created"
    rmmod execmd 2>/dev/null || true
    exit 1
fi
echo "ok: /proc/rootkitlings_cmd exists"

# Execute command
echo "id" > /proc/rootkitlings_cmd
sleep 2

# Check output
if [[ ! -f /tmp/rkl_out ]]; then
    echo "FAIL: /tmp/rkl_out not created"
    rmmod execmd 2>/dev/null || true
    exit 1
fi

if grep -q "uid=0(root)" /tmp/rkl_out; then
    echo "ok: command output contains uid=0(root)"
else
    echo "FAIL: unexpected output"
    cat /tmp/rkl_out
    rmmod execmd 2>/dev/null || true
    exit 1
fi

# Test another command
echo "hostname" > /proc/rootkitlings_cmd
sleep 2
if [[ -s /tmp/rkl_out ]]; then
    echo "ok: hostname command produced output"
else
    echo "WARN: hostname produced no output"
fi

rmmod execmd 2>/dev/null || true
rm -f /tmp/rkl_out

echo "PASS: command execution works"
