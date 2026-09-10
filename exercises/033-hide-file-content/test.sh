#!/bin/bash
set -e

MODULE="hideline"
KO="src/${MODULE}.ko"
TARGET="/tmp/rootkitlings_test.conf"

if [ ! -f "$KO" ]; then
    echo "FAIL: $KO not found. Run make first."
    exit 1
fi

echo "==> Creating test file"
cat > "$TARGET" <<'CONF'
# Normal config
setting1=value1
rootkit_secret_line=payload_here
setting2=value2
# End of config
CONF

echo "==> Content BEFORE hook:"
cat "$TARGET"

echo ""
echo "==> Loading $MODULE"
insmod "$KO"

echo "==> Content AFTER hook:"
CONTENT=$(cat "$TARGET")
echo "$CONTENT"

if echo "$CONTENT" | grep -q "rootkit_secret_line"; then
    echo ""
    echo "FAIL: secret line still visible"
    rmmod "$MODULE"
    exit 1
fi

if ! echo "$CONTENT" | grep -q "setting1"; then
    echo ""
    echo "FAIL: normal lines also hidden (overkill)"
    rmmod "$MODULE"
    exit 1
fi

if ! echo "$CONTENT" | grep -q "setting2"; then
    echo ""
    echo "FAIL: lines after secret also hidden"
    rmmod "$MODULE"
    exit 1
fi

echo ""
echo "PASS: secret line hidden, other lines intact"

echo "==> Unloading"
rmmod "$MODULE"

echo "==> Content AFTER unhook:"
cat "$TARGET"

echo "==> Cleanup"
rm -f "$TARGET"

echo "==> dmesg tail:"
dmesg | tail -5
