#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE="ftraceops"

echo "==> Chargement du module $MODULE"
insmod "$SCRIPT_DIR/src/$MODULE.ko"

echo "==> Provocation d'un openat"
cat /etc/hostname > /dev/null

echo "==> Verification dans dmesg"
sleep 0.5
DMESG=$(dmesg | tail -20)

if ! echo "$DMESG" | grep -q "ftrace hook registered"; then
    echo "FAIL: le hook ne s'est pas enregistre"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if echo "$DMESG" | grep -q "hooked function called"; then
    echo "PASS: la callback ftrace est appelee"
else
    echo "FAIL: aucun log de la callback dans dmesg"
    echo "      Verifie que .func est assigne et que tu as fait ftrace_set_filter_ip + register_ftrace_function"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

rmmod "$MODULE"
echo "PASS: ftrace_ops fonctionnel"
