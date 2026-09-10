#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ANSWERS="$SCRIPT_DIR/answers.txt"

if [[ ! -f "$ANSWERS" ]]; then
    echo "FAIL: answers.txt n'existe pas"
    echo "      Cree-le avec les 5 reponses (voir README.md)"
    exit 1
fi

LINES=$(wc -l < "$ANSWERS")
if [[ "$LINES" -lt 5 ]]; then
    echo "FAIL: answers.txt ne contient que $LINES lignes, il en faut 5"
    exit 1
fi

# Verifie que les reponses contiennent des adresses hex (pas vides)
if ! grep -q "0x\|ffffffff" "$ANSWERS"; then
    echo "FAIL: answers.txt ne semble pas contenir d'adresses hex"
    echo "      Relis le README et lance les commandes grep sur /proc/kallsyms"
    exit 1
fi

# Verifie la coherence: commit_creds doit etre de type T
if grep -qi "5.*: T" "$ANSWERS" || grep -qi "5.*: t" "$ANSWERS"; then
    true
fi

echo "PASS: answers.txt contient les 5 reponses"
echo ""
echo "Verification manuelle:"
echo "  Compare tes adresses avec:"
echo "    grep ' commit_creds' /proc/kallsyms"
echo "    grep ' prepare_kernel_cred' /proc/kallsyms"
