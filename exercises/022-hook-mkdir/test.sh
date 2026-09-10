#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE="hookmkdir"
TESTDIR="/tmp/rootkitlings_022_test"

# Copie ftrace_helper.h si absent
if [[ ! -f "$SCRIPT_DIR/src/ftrace_helper.h" ]]; then
    cp "$SCRIPT_DIR/../021-ftrace-helper/src/ftrace_helper.h" "$SCRIPT_DIR/src/"
fi

echo "==> Chargement du module $MODULE"
insmod "$SCRIPT_DIR/src/$MODULE.ko"

echo "==> Creation de $TESTDIR"
rmdir "$TESTDIR" 2>/dev/null || true
mkdir "$TESTDIR"

echo "==> Verification dans dmesg"
sleep 0.5
DMESG=$(dmesg | tail -10)

if ! echo "$DMESG" | grep -q "mkdir hook installed"; then
    echo "FAIL: le hook ne s'est pas installe"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if echo "$DMESG" | grep -q "mkdir.*rootkitlings_022_test"; then
    echo "PASS: mkdir intercepte avec le chemin"
else
    echo "FAIL: la creation du dossier n'a pas ete loggee"
    echo "      Verifie que tu lis le pathname depuis regs->si"
    rmmod "$MODULE" 2>/dev/null || true
    rmdir "$TESTDIR" 2>/dev/null || true
    exit 1
fi

rmmod "$MODULE"
rmdir "$TESTDIR" 2>/dev/null || true
echo "PASS: hook mkdir fonctionnel"
