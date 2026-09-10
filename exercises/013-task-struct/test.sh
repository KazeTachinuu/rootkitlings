#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE="taskinfo"

echo "==> Chargement du module $MODULE"
insmod "$SCRIPT_DIR/src/$MODULE.ko"

echo "==> Verification dans dmesg"
DMESG=$(dmesg | tail -5)

if ! echo "$DMESG" | grep -q "pid = "; then
    echo "FAIL: le printk du PID est absent"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if ! echo "$DMESG" | grep -q "comm = "; then
    echo "FAIL: le printk du comm est absent"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if ! echo "$DMESG" | grep -q "tgid = "; then
    echo "FAIL: le printk du TGID est absent"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

# Le comm devrait etre "insmod"
if echo "$DMESG" | grep -q "comm = insmod"; then
    echo "PASS: current->comm = insmod (correct)"
else
    COMM=$(echo "$DMESG" | grep "comm = " | head -1)
    echo "NOTE: $COMM (attendu: insmod)"
    echo "      C'est normal si tu as charge le module autrement"
fi

rmmod "$MODULE"
echo "PASS: task_struct lu correctement"
