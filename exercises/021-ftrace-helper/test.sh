#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE="easyhook"

echo "==> Chargement du module $MODULE"
insmod "$SCRIPT_DIR/src/$MODULE.ko"

echo "==> Envoi de kill -0 1 (test existence PID 1)"
kill -0 1 2>/dev/null || true

echo "==> Verification dans dmesg"
sleep 0.5
DMESG=$(dmesg | tail -10)

if ! echo "$DMESG" | grep -q "kill hook installed"; then
    echo "FAIL: le hook ne s'est pas installe"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if echo "$DMESG" | grep -q "kill(1, 0)"; then
    echo "PASS: kill(1, 0) intercepte"
elif echo "$DMESG" | grep -q "rootkitlings/021: kill"; then
    echo "PASS: kill() intercepte (arguments peut-etre dans un autre ordre)"
else
    echo "FAIL: kill() n'a pas ete intercepte"
    echo "      Verifie:"
    echo "      1. La macro HOOK dans le tableau hooks[]"
    echo "      2. La fonction hook_kill avec le printk"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

rmmod "$MODULE"
echo "PASS: ftrace_helper.h fonctionne"
