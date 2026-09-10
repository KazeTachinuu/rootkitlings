#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE="creds"

echo "==> Chargement du module $MODULE"
insmod "$SCRIPT_DIR/src/$MODULE.ko"

echo "==> Verification dans dmesg"
DMESG=$(dmesg | tail -5)

if ! echo "$DMESG" | grep -q "uid = "; then
    echo "FAIL: le printk de l'UID est absent"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if ! echo "$DMESG" | grep -q "gid = "; then
    echo "FAIL: le printk du GID est absent"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if ! echo "$DMESG" | grep -q "euid = "; then
    echo "FAIL: le printk de l'EUID est absent"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

# Si on est root, uid doit etre 0
if [[ $(id -u) -eq 0 ]]; then
    if echo "$DMESG" | grep -q "uid = 0"; then
        echo "PASS: uid = 0 (root, correct)"
    else
        echo "FAIL: tu es root mais le module n'affiche pas uid = 0"
        rmmod "$MODULE" 2>/dev/null || true
        exit 1
    fi
fi

rmmod "$MODULE"
echo "PASS: credentials lues correctement"
