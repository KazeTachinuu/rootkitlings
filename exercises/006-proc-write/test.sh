#!/bin/bash
set -e
MODULE="procwrite"

echo "==> Test exercice 006: proc-write"

dmesg -C 2>/dev/null || true

insmod src/${MODULE}.ko

echo "    Ecriture dans /proc/rootkitlings_rw..."
echo -n "hello kernel" > /proc/rootkitlings_rw

echo "    Lecture de /proc/rootkitlings_rw..."
CONTENT=$(cat /proc/rootkitlings_rw)
echo "    Contenu: '$CONTENT'"

if echo "$CONTENT" | grep -q "hello kernel"; then
    echo "    PASS: contenu ecrit et relu correctement"
else
    echo "    FAIL: contenu incorrect"
    rmmod ${MODULE} 2>/dev/null || true
    exit 1
fi

echo "    Ecriture d'un second message..."
echo -n "second msg" > /proc/rootkitlings_rw
CONTENT=$(cat /proc/rootkitlings_rw)

if echo "$CONTENT" | grep -q "second msg"; then
    echo "    PASS: second message OK"
else
    echo "    FAIL: second message incorrect: '$CONTENT'"
    rmmod ${MODULE} 2>/dev/null || true
    exit 1
fi

rmmod ${MODULE}
echo "==> 006 OK"
