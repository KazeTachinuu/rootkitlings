#!/bin/bash
set -e

SERVICE="rootkitlings.service"
SERVICE_SRC="src/$SERVICE"
SERVICE_DST="/etc/systemd/system/$SERVICE"
KO_PATH="/opt/rootkitlings/persist.ko"

echo "[039] Test: persistence via systemd service"

# Check service file exists
if [[ ! -f "$SERVICE_SRC" ]]; then
    echo "FAIL: $SERVICE_SRC not found"
    exit 1
fi

# Check it has real content (not just TODOs)
if grep -q "TODO(rootkitlings)" "$SERVICE_SRC"; then
    echo "FAIL: $SERVICE_SRC still has TODO markers"
    exit 1
fi

# Validate required fields
for field in "Description=" "After=" "Type=oneshot" "ExecStart=" "RemainAfterExit=yes" "WantedBy=multi-user.target"; do
    if ! grep -q "$field" "$SERVICE_SRC"; then
        echo "FAIL: missing or incorrect field: $field"
        exit 1
    fi
done
echo "ok: service file has correct structure"

# Check ExecStart points to the .ko
if ! grep -q "ExecStart=.*insmod.*persist.ko" "$SERVICE_SRC"; then
    echo "FAIL: ExecStart should use insmod with path to persist.ko"
    exit 1
fi
echo "ok: ExecStart uses insmod"

# Deploy and test
mkdir -p /opt/rootkitlings
cp src/persist.ko "$KO_PATH"
cp "$SERVICE_SRC" "$SERVICE_DST"
systemctl daemon-reload

# Try starting the service
systemctl start rootkitlings
if lsmod | grep -q "^persist"; then
    echo "ok: service loaded the module"
else
    echo "FAIL: module not loaded after systemctl start"
    exit 1
fi

# Enable for boot
systemctl enable rootkitlings 2>/dev/null
echo "ok: service enabled for boot"

# Cleanup
systemctl stop rootkitlings 2>/dev/null || true
rmmod persist 2>/dev/null || true

echo "PASS: systemd persistence configured"
echo "      Reboot to confirm automatic loading"
