#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE="kprobe"

echo "==> Chargement du module $MODULE"
insmod "$SCRIPT_DIR/src/$MODULE.ko"

echo "==> Provocation d'un openat (touch)"
touch /tmp/rootkitlings_016_test

echo "==> Verification dans dmesg"
sleep 0.5
DMESG=$(dmesg | tail -30)

if ! echo "$DMESG" | grep -q "kprobe registered"; then
    echo "FAIL: le module ne s'est pas charge correctement"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if echo "$DMESG" | grep -q "do_sys_openat2"; then
    echo "PASS: le pre_handler est appele pour do_sys_openat2"
else
    echo "FAIL: aucun log du pre_handler dans dmesg"
    echo "      Verifie que tu as assigne .pre_handler et que tu fais un printk dedans"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

rmmod "$MODULE"
rm -f /tmp/rootkitlings_016_test
echo "PASS: kprobe fonctionnel"
