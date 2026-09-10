#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE="hookkill"

if [[ ! -f "$SCRIPT_DIR/src/ftrace_helper.h" ]]; then
    cp "$SCRIPT_DIR/../021-ftrace-helper/src/ftrace_helper.h" "$SCRIPT_DIR/src/"
fi

echo "==> Chargement du module $MODULE"
insmod "$SCRIPT_DIR/src/$MODULE.ko"

echo "==> Envoi du signal magique 63 au PID 1"
kill -63 1 2>/dev/null || true

echo "==> Verification dans dmesg"
sleep 0.5
DMESG=$(dmesg | tail -10)

if ! echo "$DMESG" | grep -q "kill hook installed"; then
    echo "FAIL: le hook ne s'est pas installe"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if echo "$DMESG" | grep -q "magic signal"; then
    echo "PASS: signal magique 63 detecte"
else
    echo "FAIL: le signal magique n'a pas ete detecte"
    echo "      Verifie le check sur sig == MAGIC_SIGNAL dans hook_kill"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

rmmod "$MODULE"
echo "PASS: hook kill avec signal magique fonctionnel"
