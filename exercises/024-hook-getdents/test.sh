#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE="hookdents"

if [[ ! -f "$SCRIPT_DIR/src/ftrace_helper.h" ]]; then
    cp "$SCRIPT_DIR/../021-ftrace-helper/src/ftrace_helper.h" "$SCRIPT_DIR/src/"
fi

# Cree un fichier test pour le retrouver dans dmesg
touch /tmp/rootkitlings_024_marker

echo "==> Chargement du module $MODULE"
insmod "$SCRIPT_DIR/src/$MODULE.ko"

echo "==> Listing de /tmp"
ls /tmp > /dev/null

echo "==> Verification dans dmesg"
sleep 0.5
DMESG=$(dmesg | tail -50)

if ! echo "$DMESG" | grep -q "getdents64 hook installed"; then
    echo "FAIL: le hook ne s'est pas installe"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if echo "$DMESG" | grep -q "entry: rootkitlings_024_marker"; then
    echo "PASS: les entries de directory sont loggees"
elif echo "$DMESG" | grep -q "entry:"; then
    echo "PASS: des entries sont loggees (le fichier marker pas vu, peut-etre trop de lignes)"
else
    echo "FAIL: aucune entry de directory dans dmesg"
    echo "      Complete le TODO: la boucle while sur le buffer"
    rmmod "$MODULE" 2>/dev/null || true
    rm -f /tmp/rootkitlings_024_marker
    exit 1
fi

rmmod "$MODULE"
rm -f /tmp/rootkitlings_024_marker
echo "PASS: hook getdents64 fonctionnel"
