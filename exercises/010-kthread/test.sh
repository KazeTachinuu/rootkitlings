#!/bin/bash
set -e
MODULE="kthread"

echo "==> Test exercice 010: kthread"

dmesg -C 2>/dev/null || true

insmod src/${MODULE}.ko

if dmesg | grep -q "rootkitlings/010: kthread started"; then
    echo "    PASS: kthread demarre"
else
    echo "    FAIL: message de demarrage absent"
    rmmod ${MODULE} 2>/dev/null || true
    exit 1
fi

echo "    Attente de 3 secondes..."
sleep 3

if dmesg | grep -q "rootkitlings/010: tick 1"; then
    echo "    PASS: premier tick"
else
    echo "    FAIL: pas de tick"
    rmmod ${MODULE} 2>/dev/null || true
    exit 1
fi

if dmesg | grep -q "rootkitlings/010: tick 2"; then
    echo "    PASS: deuxieme tick"
else
    echo "    FAIL: pas de second tick"
    rmmod ${MODULE} 2>/dev/null || true
    exit 1
fi

echo "    Verification ps..."
if ps aux | grep -v grep | grep -q rootkitlings; then
    echo "    PASS: thread visible dans ps"
else
    echo "    NOTE: thread pas visible dans ps (pas bloquant)"
fi

rmmod ${MODULE}

if dmesg | grep -q "rootkitlings/010: kthread stopped"; then
    echo "    PASS: arret propre"
else
    echo "    FAIL: message d'arret absent"
    exit 1
fi

echo "==> 010 OK"
