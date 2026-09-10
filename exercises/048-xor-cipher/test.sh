#!/bin/bash
set -e

echo "[048] Test: XOR cipher"

if grep -q "TODO(rootkitlings)" src/xor.c; then
    echo "FAIL: src/xor.c still has TODO markers"
    exit 1
fi

python3 test_xor_server.py &
SRV_PID=$!
sleep 1

insmod src/xor.ko
sleep 2

wait $SRV_PID
SRV_EXIT=$?

rmmod xor 2>/dev/null || true

if [[ $SRV_EXIT -eq 0 ]]; then
    echo "PASS: XOR encryption/decryption works"
else
    echo "FAIL: XOR test failed"
    dmesg | tail -10
    exit 1
fi
