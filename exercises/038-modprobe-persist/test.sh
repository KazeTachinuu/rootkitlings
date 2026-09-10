#!/bin/bash
set -e

CONF="/etc/modules-load.d/rootkitlings.conf"
MOD_DIR="/lib/modules/$(uname -r)/extra"
MOD_NAME="persist"

echo "[038] Test: persistence via modules-load.d"

# Check .conf file
if [[ ! -f "$CONF" ]]; then
    echo "FAIL: $CONF does not exist"
    echo "      Copy src/rootkitlings.conf to $CONF"
    exit 1
fi

# Check .conf content
if ! grep -q "^${MOD_NAME}$" "$CONF"; then
    echo "FAIL: $CONF does not contain '$MOD_NAME'"
    exit 1
fi
echo "ok: .conf file exists with correct content"

# Check .ko in modules directory
if [[ ! -f "$MOD_DIR/${MOD_NAME}.ko" ]]; then
    echo "FAIL: ${MOD_NAME}.ko not found in $MOD_DIR"
    echo "      Run: sudo mkdir -p $MOD_DIR && sudo cp src/${MOD_NAME}.ko $MOD_DIR/"
    exit 1
fi
echo "ok: .ko in $MOD_DIR"

# Check depmod knows about it
if ! modprobe -n "$MOD_NAME" 2>/dev/null; then
    echo "FAIL: modprobe does not know '$MOD_NAME'"
    echo "      Run: sudo depmod -a"
    exit 1
fi
echo "ok: depmod recognizes the module"

# Try loading via modprobe (same path as boot)
modprobe "$MOD_NAME"
if lsmod | grep -q "^${MOD_NAME}"; then
    echo "ok: modprobe loaded the module"
    rmmod "$MOD_NAME"
else
    echo "FAIL: modprobe did not load the module"
    exit 1
fi

echo "PASS: persistence via modules-load.d is configured"
echo "      Reboot the VM to confirm it loads automatically"
