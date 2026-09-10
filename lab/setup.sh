#!/bin/bash
# Installe les prérequis lab sur Arch Linux.
#
# Ce lab utilise QEMU + KVM avec une image Debian jetable pour tester le rootkit
# en isolation totale (aucun accès RW au FS de l'hôte).
#
# Usage : ./setup.sh

set -e

echo "==> Vérification qu'on tourne sur Arch"
if ! command -v pacman &>/dev/null; then
    echo "AVERTISSEMENT : ce script est prévu pour Arch Linux."
    echo "Sur Ubuntu 24.10 (env officiel EPITA) :"
    echo "  sudo apt install qemu-system-x86 qemu-utils genisoimage build-essential gdb linux-headers-\$(uname -r)"
    exit 1
fi

echo "==> Installation des paquets"
sudo pacman -Syu --needed --noconfirm \
    base-devel \
    linux-headers \
    qemu-full \
    cdrtools \
    curl \
    gdb \
    git

echo "==> Vérification que KVM est utilisable"
if [[ ! -w /dev/kvm ]]; then
    echo ""
    echo "AVERTISSEMENT : /dev/kvm n'est pas accessible par ton user."
    echo "Ajoute-toi au groupe kvm :"
    echo "  sudo usermod -aG kvm \$USER"
    echo "Puis reconnecte-toi (logout/login)."
    echo ""
fi

echo ""
echo "==> Prérequis installés."
echo ""
echo "Étapes suivantes :"
echo "  1. ./build-vm.sh     # construit l'image Debian jetable (une fois)"
echo "  2. cd ../tps/tp1 && make    # compile ton module"
echo "  3. ../../lab/vm.sh --share . --snapshot    # boot la VM avec ton .ko en RO"
