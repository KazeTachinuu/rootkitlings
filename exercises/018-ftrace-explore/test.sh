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

# Verifie que les fonctions getdents et kill sont mentionnees
if grep -qi "getdents" "$ANSWERS"; then
    echo "OK: getdents mentionne"
else
    echo "WARN: getdents pas trouve dans les reponses"
fi

if grep -qi "kill" "$ANSWERS"; then
    echo "OK: kill mentionne"
else
    echo "WARN: kill pas trouve dans les reponses"
fi

if grep -qi "nop" "$ANSWERS"; then
    echo "OK: tracer nop mentionne"
else
    echo "WARN: nop pas trouve dans les reponses"
fi

echo ""
echo "PASS: answers.txt contient des reponses"
echo "      Compare tes reponses avec les commandes du README"
