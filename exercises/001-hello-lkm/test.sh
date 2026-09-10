#!/bin/bash
# Test pour l'exercice 001: challenge.ko
# A lancer dans la VM (vng ou qemu), pas sur l'hote.
set -e

KO="src/challenge.ko"
MODULE="challenge"

if [[ ! -f "$KO" ]]; then
    echo "FAIL: $KO n'existe pas. Lance 'make' d'abord."
    exit 1
fi

echo "==> Verification modinfo"
INFO=$(modinfo "$KO" 2>&1)

if ! echo "$INFO" | grep -qi "author"; then
    echo "FAIL: modinfo ne montre pas d'auteur. Ajoute MODULE_AUTHOR."
    exit 1
fi

if ! echo "$INFO" | grep -qi "description"; then
    echo "FAIL: modinfo ne montre pas de description. Ajoute MODULE_DESCRIPTION."
    exit 1
fi

echo "    modinfo OK (auteur + description present)"

echo "==> Chargement du module"
dmesg --clear 2>/dev/null || true
insmod "$KO"

echo "==> Verification dmesg (init)"
sleep 0.5
DMESG=$(dmesg 2>/dev/null || cat /dev/kmsg 2>/dev/null | head -20)

if ! echo "$DMESG" | grep -q "reporting for duty"; then
    echo "FAIL: dmesg ne contient pas 'reporting for duty' apres insmod."
    echo "Contenu dmesg:"
    echo "$DMESG"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

echo "    init OK"

echo "==> Dechargement du module"
rmmod "$MODULE"

echo "==> Verification dmesg (exit)"
sleep 0.5
DMESG=$(dmesg 2>/dev/null || cat /dev/kmsg 2>/dev/null | head -30)

if ! echo "$DMESG" | grep -q "out"; then
    echo "FAIL: dmesg ne contient pas 'out' apres rmmod."
    echo "Contenu dmesg:"
    echo "$DMESG"
    exit 1
fi

echo "    exit OK"
echo ""
echo "PASS: exercice 001 termine."
