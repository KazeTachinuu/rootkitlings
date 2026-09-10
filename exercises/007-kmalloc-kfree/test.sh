#!/bin/bash
set -e
MODULE="memory"

echo "==> Test exercice 007: kmalloc-kfree"

dmesg -C 2>/dev/null || true

insmod src/${MODULE}.ko

if dmesg | grep -q "rootkitlings/007: allocated OK"; then
    echo "    PASS: allocation reussie"
else
    echo "    FAIL: message d'allocation absent"
    rmmod ${MODULE} 2>/dev/null || true
    exit 1
fi

rmmod ${MODULE}

if dmesg | grep -q "rootkitlings/007: freed"; then
    echo "    PASS: liberation OK"
else
    echo "    FAIL: message de liberation absent"
    exit 1
fi

echo "==> 007 OK"
