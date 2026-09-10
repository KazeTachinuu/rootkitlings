#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE="kretprobe"

echo "==> Chargement du module $MODULE"
insmod "$SCRIPT_DIR/src/$MODULE.ko"

echo "==> Creation d'un dossier test"
rmdir /tmp/rootkitlings_017_test 2>/dev/null || true
mkdir /tmp/rootkitlings_017_test

echo "==> Verification dans dmesg"
sleep 0.5
DMESG=$(dmesg | tail -10)

if ! echo "$DMESG" | grep -q "kretprobe registered"; then
    echo "FAIL: le module ne s'est pas charge correctement"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if echo "$DMESG" | grep -q "do_mkdirat returned"; then
    RETVAL=$(echo "$DMESG" | grep "do_mkdirat returned" | tail -1 | grep -o "returned [0-9-]*")
    echo "PASS: kretprobe capture le retour: $RETVAL"
else
    echo "FAIL: aucun log de retour dans dmesg"
    echo "      Verifie que tu as assigne .handler et que ton ret_handler fait un printk"
    rmmod "$MODULE" 2>/dev/null || true
    rmdir /tmp/rootkitlings_017_test 2>/dev/null || true
    exit 1
fi

rmmod "$MODULE"
rmdir /tmp/rootkitlings_017_test 2>/dev/null || true
echo "PASS: kretprobe fonctionnel"
