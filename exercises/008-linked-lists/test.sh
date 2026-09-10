#!/bin/bash
set -e
MODULE="lists"

echo "==> Test exercice 008: linked-lists"

dmesg -C 2>/dev/null || true

insmod src/${MODULE}.ko

FAIL=0
for val in 10 20 30; do
    if dmesg | grep -q "rootkitlings/008: value=${val}"; then
        echo "    PASS: value=${val} trouve"
    else
        echo "    FAIL: value=${val} absent"
        FAIL=1
    fi
done

rmmod ${MODULE}

if dmesg | grep -q "rootkitlings/008: list freed"; then
    echo "    PASS: liste liberee"
else
    echo "    FAIL: message de liberation absent"
    FAIL=1
fi

if [[ $FAIL -eq 1 ]]; then
    echo "==> 008 FAIL"
    exit 1
fi

echo "==> 008 OK"
