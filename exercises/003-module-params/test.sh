#!/bin/bash
set -e
MODULE="params"

echo "==> Test exercice 003: module-params"

dmesg -C 2>/dev/null || true

echo "    Chargement avec myint=42 mystr=hello..."
insmod src/${MODULE}.ko myint=42 mystr="hello"

if dmesg | grep -q "rootkitlings/003: myint=42 mystr=hello"; then
    echo "    PASS: parametres correctement recus"
else
    echo "    FAIL: parametres pas affiches correctement"
    dmesg | grep "rootkitlings/003" || true
    rmmod ${MODULE} 2>/dev/null || true
    exit 1
fi

rmmod ${MODULE}

echo "    Verification de modinfo..."
if modinfo src/${MODULE}.ko | grep -q "parm:.*myint"; then
    echo "    PASS: myint visible dans modinfo"
else
    echo "    FAIL: myint absent de modinfo (MODULE_PARM_DESC manquant ?)"
    exit 1
fi

echo "==> 003 OK"
