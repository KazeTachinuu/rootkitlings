#!/bin/bash
# Boot rapide via virtme-ng pour les TPs "Rootkit 101" ou l'isolation forte
# n'est pas critique (hello LKM, printk, exploration de /proc, ftrace de base).
#
# Trade-off :
#   ✅ Boot en ~3 secondes, itération ultra rapide
#   ⚠️  Le FS de l'hôte est monté READ-ONLY dans la VM (pas RW).
#       Un rootkit qui écrit dans /etc/, /tmp, etc. échouera silencieusement.
#       Pour les TPs qui font ça, utilise ./vm.sh (QEMU + Debian, isolation totale).
#
# Usage :
#   ./vng.sh                # boot standard, FS hôte read-only
#   ./vng.sh --rwdir DIR    # partage DIR en RW (dossier du .ko courant p.ex.)
#   ./vng.sh --help

set -e

RWDIR=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --rwdir) RWDIR="$2"; shift 2 ;;
        --help)
            cat <<EOF
Usage : $0 [--rwdir DIR]

Fast boot via virtme-ng pour Rootkit 101. FS hôte en read-only.

Options :
  --rwdir DIR   Partage DIR en read-write. À utiliser SEULEMENT pour le dossier
                de ton .ko en cours de dev, pas ton \$HOME.

Quitter la VM : Ctrl+D ou 'exit'.

Pour des TPs plus avancés (hooks, hide, persistence), utilise ./vm.sh.
EOF
            exit 0 ;;
        *) echo "Option inconnue : $1" >&2; exit 1 ;;
    esac
done

if ! command -v vng &>/dev/null; then
    echo "ERREUR : virtme-ng (vng) n'est pas installé."
    echo "        yay -S virtme-ng"
    echo "        # ou : pipx install virtme-ng"
    exit 1
fi

VNG_ARGS=(
    --user root
    --memory 1G
)

if [[ -n "$RWDIR" ]]; then
    RWDIR="$(cd "$RWDIR" && pwd)"
    echo "==> $RWDIR partagé en RW dans la VM"
    VNG_ARGS+=(--rwdir "$RWDIR")
fi

echo "==> Boot virtme-ng (Ctrl+D pour quitter)"
exec vng "${VNG_ARGS[@]}"
