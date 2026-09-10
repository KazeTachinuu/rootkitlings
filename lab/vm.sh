#!/bin/bash
# Boot une VM QEMU jetable pour tester un rootkit en dev.
# Isolation TOTALE : image disque séparée, pas de FS hôte partagé en RW.
# Le seul lien = un dossier partagé en READ-ONLY (le dossier du module .ko).
#
# Usage :
#   ./vm.sh                  # boot standard
#   ./vm.sh --share DIR      # partage DIR en RO dans la VM sous /mnt/host
#   ./vm.sh --gdb            # gdbstub sur :1234
#   ./vm.sh --snapshot       # ne persiste rien sur le disque (rollback auto)
#   ./vm.sh --help
#
# Prérequis : ./build-vm.sh a produit disk.qcow2 et cloud-init.iso.

set -e

LAB_DIR="$(cd "$(dirname "$0")" && pwd)"
DISK="$LAB_DIR/build/disk.qcow2"
CLOUD_INIT="$LAB_DIR/build/cloud-init.iso"

SHARE_DIR=""
GDB=0
SNAPSHOT=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --share) SHARE_DIR="$2"; shift 2 ;;
        --gdb) GDB=1; shift ;;
        --snapshot) SNAPSHOT=1; shift ;;
        --help)
            cat <<EOF
Usage : $0 [--share DIR] [--gdb] [--snapshot]

Options :
  --share DIR   Partage DIR (read-only) sous /mnt/host dans la VM.
                Typiquement le dossier de ton .ko compilé.
  --gdb         Expose un gdbstub sur localhost:1234 (attach avec gdb vmlinux).
  --snapshot    Rollback automatique a la sortie, la VM ne persiste rien.
  --help        Cette aide.

Login VM : root / root (via cloud-init, à changer avant tout usage sérieux).
Quitter la VM : 'poweroff' ou Ctrl+A puis X pour tuer QEMU.
EOF
            exit 0 ;;
        *) echo "Option inconnue : $1" >&2; exit 1 ;;
    esac
done

if [[ ! -f "$DISK" ]]; then
    echo "ERREUR : $DISK n'existe pas."
    echo "        Lance d'abord ./build-vm.sh pour construire l'image."
    exit 1
fi

QEMU_ARGS=(
    -enable-kvm
    -cpu host
    -smp 2
    -m 2G

    # Disk
    -drive file="$DISK",if=virtio,format=qcow2
    -drive file="$CLOUD_INIT",if=virtio,format=raw,readonly=on

    # Network : SLIRP (NAT), pas de bridge → aucun paquet ne sort sans passer par l'hôte
    -netdev user,id=net0,hostfwd=tcp::2222-:22
    -device virtio-net-pci,netdev=net0

    # Console serie (pas de fenetre graphique, plus rapide)
    -nographic
    -serial mon:stdio
)

if [[ $SNAPSHOT -eq 1 ]]; then
    # -snapshot : toutes les écritures dans un overlay temporaire, jeté à l'exit
    QEMU_ARGS+=(-snapshot)
    echo "==> Mode snapshot : la VM ne persistera aucune modif"
fi

if [[ -n "$SHARE_DIR" ]]; then
    SHARE_DIR="$(cd "$SHARE_DIR" && pwd)"
    echo "==> Partage READ-ONLY de $SHARE_DIR → /mnt/host dans la VM"
    QEMU_ARGS+=(
        -virtfs local,path="$SHARE_DIR",mount_tag=hostshare,security_model=none,readonly=on
    )
fi

if [[ $GDB -eq 1 ]]; then
    echo "==> gdbstub sur localhost:1234"
    echo "    Dans un autre terminal :"
    echo "      cd \$KERNEL_SRC && gdb vmlinux"
    echo "      (gdb) target remote :1234"
    QEMU_ARGS+=(-s -S)
fi

echo "==> Boot QEMU (Ctrl+A puis X pour tuer, ou 'poweroff' dans la VM)"
echo "    SSH depuis l'hôte : ssh -p 2222 root@localhost"
echo ""
exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
