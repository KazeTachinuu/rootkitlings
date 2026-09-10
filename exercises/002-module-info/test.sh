#!/bin/bash
set -e
MODULE="info"

echo "==> Test exercice 002: module-info"

FAIL=0

echo "    Verification des metadonnees avec modinfo..."

if modinfo src/${MODULE}.ko | grep -q "license:"; then
    echo "    PASS: license present"
else
    echo "    FAIL: license absent"
    FAIL=1
fi

if modinfo src/${MODULE}.ko | grep -q "author:"; then
    echo "    PASS: author present"
else
    echo "    FAIL: author absent"
    FAIL=1
fi

if modinfo src/${MODULE}.ko | grep -q "description:"; then
    echo "    PASS: description present"
else
    echo "    FAIL: description absent"
    FAIL=1
fi

if modinfo src/${MODULE}.ko | grep -q "version:"; then
    echo "    PASS: version present"
else
    echo "    FAIL: version absent"
    FAIL=1
fi

if [[ $FAIL -eq 1 ]]; then
    echo "==> 002 FAIL"
    exit 1
fi

echo "    Chargement/dechargement pour verifier que le module fonctionne..."
dmesg -C 2>/dev/null || true
insmod src/${MODULE}.ko
rmmod ${MODULE}

echo "==> 002 OK"
