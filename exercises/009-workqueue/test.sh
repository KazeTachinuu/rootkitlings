#!/bin/bash
set -e
MODULE="workq"

echo "==> Test exercice 009: workqueue"

dmesg -C 2>/dev/null || true

insmod src/${MODULE}.ko

echo "    Attente de 3 secondes..."
sleep 3

if dmesg | grep -q "rootkitlings/009: tick 1"; then
    echo "    PASS: premier tick"
else
    echo "    FAIL: pas de tick"
    rmmod ${MODULE} 2>/dev/null || true
    exit 1
fi

if dmesg | grep -q "rootkitlings/009: tick 2"; then
    echo "    PASS: deuxieme tick (le work se re-planifie)"
else
    echo "    FAIL: pas de second tick (re-planification manquante ?)"
    rmmod ${MODULE} 2>/dev/null || true
    exit 1
fi

rmmod ${MODULE}

if dmesg | grep -q "rootkitlings/009: workqueue stopped"; then
    echo "    PASS: arret propre"
else
    echo "    FAIL: message d'arret absent"
    exit 1
fi

echo "==> 009 OK"
