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

# Verifie la reponse 1: getdents64 est le syscall 217
if grep -qi "217" "$ANSWERS"; then
    echo "OK: numero de syscall getdents64 present"
else
    echo "WARN: 217 pas trouve dans answers.txt (getdents64 = 217)"
fi

# Verifie la reponse 2: le wrapper
if grep -qi "__x64_sys_getdents64" "$ANSWERS"; then
    echo "OK: nom du wrapper present"
else
    echo "WARN: __x64_sys_getdents64 pas trouve dans answers.txt"
fi

# Verifie la reponse 3: le fichier
if grep -qi "readdir" "$ANSWERS"; then
    echo "OK: fichier source mentionne"
else
    echo "WARN: fs/readdir.c pas trouve dans answers.txt"
fi

echo ""
echo "PASS: answers.txt contient des reponses"
echo "      Verifie manuellement sur Elixir Bootlin que tes reponses sont correctes"
