#!/bin/bash
set -e
MODULE="myproc"

echo "==> Test exercice 004: proc-create"

dmesg -C 2>/dev/null || true

echo "    Chargement du module..."
insmod src/${MODULE}.ko

if [[ -f /proc/rootkitlings ]]; then
    echo "    PASS: /proc/rootkitlings existe"
else
    echo "    FAIL: /proc/rootkitlings n'existe pas"
    rmmod ${MODULE} 2>/dev/null || true
    exit 1
fi

CONTENT=$(cat /proc/rootkitlings)
if echo "$CONTENT" | grep -q "rootkitlings/004: proc entry works"; then
    echo "    PASS: contenu correct"
else
    echo "    FAIL: contenu inattendu: $CONTENT"
    rmmod ${MODULE} 2>/dev/null || true
    exit 1
fi

rmmod ${MODULE}

if [[ ! -f /proc/rootkitlings ]]; then
    echo "    PASS: /proc/rootkitlings supprime apres rmmod"
else
    echo "    FAIL: /proc/rootkitlings existe encore apres rmmod"
    exit 1
fi

echo "==> 004 OK"
