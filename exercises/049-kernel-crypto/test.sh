#!/bin/bash
set -e

echo "[049] Test: kernel crypto API (AES-CBC)"

if grep -q "TODO(rootkitlings)" src/kcrypto.c; then
    echo "FAIL: src/kcrypto.c still has TODO markers"
    exit 1
fi

insmod src/kcrypto.ko

if [[ ! -e /proc/rootkitlings_crypto ]]; then
    echo "FAIL: /proc/rootkitlings_crypto not created"
    rmmod kcrypto 2>/dev/null || true
    exit 1
fi
echo "ok: /proc entry created"

# Write test data
echo "test message 123" > /proc/rootkitlings_crypto
sleep 1

# Check roundtrip
if dmesg | tail -10 | grep -q "049: roundtrip OK"; then
    echo "ok: AES encrypt/decrypt roundtrip successful"
else
    echo "FAIL: roundtrip failed"
    dmesg | tail -10
    rmmod kcrypto 2>/dev/null || true
    exit 1
fi

# Check decrypted matches original
if dmesg | tail -10 | grep -q "049: decrypted: test message 123"; then
    echo "ok: decrypted text matches original"
else
    echo "FAIL: decrypted text does not match"
    dmesg | tail -10
    rmmod kcrypto 2>/dev/null || true
    exit 1
fi

rmmod kcrypto 2>/dev/null || true

echo "PASS: kernel crypto API works"
