#!/bin/bash
set -e

echo "[050] Test: encrypted C2"

if grep -q "TODO(rootkitlings)" src/cryptoc2.c; then
    echo "FAIL: src/cryptoc2.c still has TODO markers"
    exit 1
fi

# Check python cryptography module
if ! python3 -c "from cryptography.hazmat.primitives.ciphers import Cipher" 2>/dev/null; then
    echo "FAIL: python3 cryptography module required"
    echo "      pip3 install cryptography"
    exit 1
fi

python3 test_crypto_c2server.py &
SRV_PID=$!
sleep 1

insmod src/cryptoc2.ko
sleep 5

wait $SRV_PID
SRV_EXIT=$?

rmmod cryptoc2 2>/dev/null || true
rm -f /tmp/rkl_out

if [[ $SRV_EXIT -eq 0 ]]; then
    echo ""
    echo "PASS: encrypted C2 works"
    echo ""
    echo "Bravo. Tu as un rootkit C2 complet avec chiffrement AES-CBC."
    echo "Combine ca avec les techniques de hiding (026-033) et persistence (038-041)"
    echo "pour le projet WLKOM."
else
    echo "FAIL: encrypted C2 test failed"
    dmesg | tail -15
    exit 1
fi
