#!/bin/bash
set -e
MODULE="procread"

echo "==> Test exercice 005: proc-read"

dmesg -C 2>/dev/null || true

insmod src/${MODULE}.ko

CONTENT=$(cat /proc/rootkitlings_read)
echo "    Contenu: $CONTENT"

if echo "$CONTENT" | grep -q "rootkitlings/005: reader="; then
    echo "    PASS: format correct"
else
    echo "    FAIL: format incorrect"
    rmmod ${MODULE} 2>/dev/null || true
    exit 1
fi

if echo "$CONTENT" | grep -q "pid="; then
    echo "    PASS: PID present"
else
    echo "    FAIL: PID absent"
    rmmod ${MODULE} 2>/dev/null || true
    exit 1
fi

rmmod ${MODULE}
echo "==> 005 OK"
