#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE="cleanup"

if [[ ! -f "$SCRIPT_DIR/src/ftrace_helper.h" ]]; then
    cp "$SCRIPT_DIR/../021-ftrace-helper/src/ftrace_helper.h" "$SCRIPT_DIR/src/"
fi

echo "==> Chargement du module $MODULE"
insmod "$SCRIPT_DIR/src/$MODULE.ko"

echo "==> Provocation du hook"
cat /etc/hostname > /dev/null

echo "==> Dechargement du module (ne doit PAS panic)"
rmmod "$MODULE"

echo "==> Verification dans dmesg"
DMESG=$(dmesg | tail -5)

if echo "$DMESG" | grep -q "cleanup done"; then
    echo "PASS: cleanup propre, pas de panic"
else
    echo "FAIL: le message de cleanup est absent"
    echo "      (si tu vois ce message, c'est que le kernel n'a pas crash,"
    echo "       mais verifie que tu as bien ajoute synchronize_rcu)"
    exit 1
fi

# Verifie que synchronize_rcu est dans le source
if grep -q "synchronize_rcu" "$SCRIPT_DIR/src/cleanup.c"; then
    echo "PASS: synchronize_rcu est present dans le code"
else
    echo "WARN: synchronize_rcu absent du code source"
    echo "      Le module fonctionne sur monocoeur, mais pas sur SMP"
fi

echo "PASS: cleanup hook fonctionnel"
