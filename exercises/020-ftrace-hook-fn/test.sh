#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE="ftracehook"

echo "==> Chargement du module $MODULE"
insmod "$SCRIPT_DIR/src/$MODULE.ko"

echo "==> Provocation d'un openat"
cat /etc/hostname > /dev/null

echo "==> Verification dans dmesg"
sleep 0.5
DMESG=$(dmesg | tail -20)

if ! echo "$DMESG" | grep -q "hook active"; then
    echo "FAIL: le hook ne s'est pas active"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if echo "$DMESG" | grep -q "openat2 hooked"; then
    echo "PASS: la redirection fonctionne, hook_openat2 est appelee"
else
    echo "FAIL: hook_openat2 n'est pas appelee"
    echo "      Verifie:"
    echo "      1. Le printk dans hook_openat2"
    echo "      2. Le regs->ip = (unsigned long) hook_openat2 dans la callback"
    echo "      3. Le check within_module pour l'anti-recursion"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

# Verifie que le systeme fonctionne toujours (l'originale est appelee)
if ls /tmp > /dev/null 2>&1; then
    echo "PASS: la fonction originale est toujours appelee (systeme fonctionnel)"
else
    echo "FAIL: la fonction originale n'est pas appelee (systeme casse)"
fi

rmmod "$MODULE"
echo "PASS: hook ftrace complet fonctionnel"
