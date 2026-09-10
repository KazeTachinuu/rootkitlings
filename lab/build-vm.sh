#!/bin/bash
# Construit une image Debian minimale pour la VM de test rootkit.
# Idempotent : à relancer, il rebuild proprement.
#
# Résultat :
#   build/disk.qcow2       ← image Debian bootable
#   build/cloud-init.iso   ← config initiale (user root/root, ssh, etc.)

set -e

LAB_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$LAB_DIR/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Debian 12 (bookworm) cloud image, petite (~350 MB), pre-cloud-init
IMG_URL="https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-nocloud-amd64.qcow2"
IMG_NAME="debian-12-nocloud-amd64.qcow2"

if [[ ! -f "$IMG_NAME" ]]; then
    echo "==> Téléchargement de l'image Debian nocloud"
    curl -L -o "$IMG_NAME" "$IMG_URL"
fi

echo "==> Copie en disk.qcow2 (image de travail)"
cp -f "$IMG_NAME" disk.qcow2

echo "==> Redimension à 8 GB (place pour builds kernel headers si besoin)"
qemu-img resize disk.qcow2 8G

echo "==> Génération de la config cloud-init"
mkdir -p cloud-init
cat > cloud-init/user-data <<'EOF'
#cloud-config
hostname: wlkom-victim
manage_etc_hosts: true
users:
  - name: root
    lock_passwd: false
    plain_text_passwd: root
    shell: /bin/bash
ssh_pwauth: true
chpasswd:
  expire: false
  users:
    - {name: root, password: root, type: text}
packages:
  - build-essential
  - linux-headers-amd64
  - gdb
  - vim
  - strace
  - ltrace
  - net-tools
  - iproute2
runcmd:
  - mkdir -p /mnt/host
  - echo "hostshare /mnt/host 9p trans=virtio,version=9p2000.L,ro 0 0" >> /etc/fstab
  - mount /mnt/host || true
EOF

cat > cloud-init/meta-data <<'EOF'
instance-id: wlkom-victim
local-hostname: wlkom-victim
EOF

if command -v genisoimage &>/dev/null; then
    ISO_TOOL=genisoimage
elif command -v mkisofs &>/dev/null; then
    ISO_TOOL=mkisofs
else
    echo "ERREUR : ni genisoimage ni mkisofs installé."
    echo "        sudo pacman -S cdrtools    (Arch)"
    echo "        sudo apt install genisoimage    (Debian)"
    exit 1
fi

$ISO_TOOL -output cloud-init.iso -volid cidata -joliet -rock \
    cloud-init/user-data cloud-init/meta-data

echo ""
echo "==> Image construite : $BUILD_DIR/disk.qcow2"
echo "==> Cloud-init      : $BUILD_DIR/cloud-init.iso"
echo ""
echo "Premier boot va prendre ~1 min (cloud-init installe les paquets)."
echo "Lance : ./vm.sh"
