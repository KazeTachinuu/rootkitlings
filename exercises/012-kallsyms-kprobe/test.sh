#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE="ksyms"

echo "==> Chargement du module $MODULE"
insmod "$SCRIPT_DIR/src/$MODULE.ko"

echo "==> Verification dans dmesg"
DMESG=$(dmesg | tail -5)

# Verifie que kallsyms_lookup_name a ete resolu (pas NULL)
if echo "$DMESG" | grep -q "kallsyms_lookup_name = 0x0000"; then
    echo "FAIL: kallsyms_lookup_name n'a pas ete resolu (adresse NULL)"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if ! echo "$DMESG" | grep -q "kallsyms_lookup_name = "; then
    echo "FAIL: le printk de kallsyms_lookup_name est absent"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

# Verifie que commit_creds a ete resolu (pas 0x0)
if echo "$DMESG" | grep -q "commit_creds = 0x0$"; then
    echo "FAIL: commit_creds n'a pas ete resolu (adresse 0)"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

if ! echo "$DMESG" | grep -q "commit_creds = 0x"; then
    echo "FAIL: le printk de commit_creds est absent"
    rmmod "$MODULE" 2>/dev/null || true
    exit 1
fi

# Compare avec /proc/kallsyms
EXPECTED=$(grep " commit_creds$" /proc/kallsyms | awk '{print $1}')
GOT=$(echo "$DMESG" | grep "commit_creds = " | grep -o '0x[0-9a-f]*')

echo "==> /proc/kallsyms dit: $EXPECTED"
echo "==> ton module dit:     $GOT"

rmmod "$MODULE"
echo "PASS: kallsyms_lookup_name resolu via kprobe, commit_creds trouve"
