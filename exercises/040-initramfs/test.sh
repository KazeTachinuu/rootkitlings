#!/bin/bash
set -e

MOD_NAME="persist"
INITRAMFS_MODS="/etc/initramfs-tools/modules"
MOD_DIR="/lib/modules/$(uname -r)/extra"

echo "[040] Test: persistence via initramfs"

# Check module is in /lib/modules
if [[ ! -f "$MOD_DIR/${MOD_NAME}.ko" ]]; then
    echo "FAIL: ${MOD_NAME}.ko not found in $MOD_DIR"
    echo "      Run: sudo mkdir -p $MOD_DIR && sudo cp src/${MOD_NAME}.ko $MOD_DIR/ && sudo depmod -a"
    exit 1
fi
echo "ok: .ko in $MOD_DIR"

# Check module listed in initramfs-tools/modules
if ! grep -q "^${MOD_NAME}$" "$INITRAMFS_MODS"; then
    echo "FAIL: '$MOD_NAME' not in $INITRAMFS_MODS"
    echo "      Add the line: $MOD_NAME"
    exit 1
fi
echo "ok: module listed in $INITRAMFS_MODS"

# Check initramfs was rebuilt (module is inside)
INITRD="/boot/initrd.img-$(uname -r)"
if [[ ! -f "$INITRD" ]]; then
    echo "WARN: $INITRD not found, skipping initramfs content check"
else
    if lsinitramfs "$INITRD" 2>/dev/null | grep -q "${MOD_NAME}.ko"; then
        echo "ok: module found in initramfs"
    else
        echo "FAIL: module not in initramfs"
        echo "      Run: sudo update-initramfs -u"
        exit 1
    fi
fi

echo "PASS: initramfs persistence configured"
echo "      Reboot to confirm the module loads before userspace"
