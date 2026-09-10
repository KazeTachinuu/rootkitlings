# Exercice 011: lire /proc/kallsyms

## Le mecanisme

Le kernel maintient une table de tous ses symboles (fonctions, variables globales)
dans `/proc/kallsyms`. C'est cette table qui permet a des outils comme `perf`,
`ftrace`, ou un debugger de traduire une adresse memoire en nom de fonction.

La table est generee au build du kernel par `scripts/kallsyms.c`, qui parse
le fichier `System.map` et encode les symboles dans une structure compacte.
Au runtime, le fichier `/proc/kallsyms` est expose par `kernel/kallsyms.c`.

Chaque ligne a ce format:

```
adresse type nom [module]
```

Exemple:
```
ffffffff81234560 T commit_creds
ffffffff81234780 t cred_alloc_blank
ffffffffc0001000 t my_func    [my_module]
```

Les types importants:
- `T`/`t` : fonction dans la section .text (T = exportee, t = locale)
- `D`/`d` : variable dans .data
- `R`/`r` : variable dans .rodata (read-only)
- `B`/`b` : variable dans .bss (non initialisee)

La config kernel `CONFIG_KALLSYMS` active cette table. Sans elle, les adresses
dans `/proc/kallsyms` sont masquees (affichees 0) sauf pour root, controle
par `/proc/sys/kernel/kptr_restrict`.

## Ce qu'il faut comprendre AVANT de chercher

1. Ouvre Elixir Bootlin et regarde le code qui genere `/proc/kallsyms`:
   https://elixir.bootlin.com/linux/latest/source/kernel/kallsyms.c

2. Cherche la fonction `kallsyms_lookup_name` dans ce fichier. Note qu'elle
   n'est plus exportee aux modules depuis le kernel 5.7 (commit 0bd476e6c671).
   C'est un point crucial pour la suite.

3. Regarde `scripts/kallsyms.c` pour comprendre comment la table est construite
   au moment de la compilation du kernel:
   https://elixir.bootlin.com/linux/latest/source/scripts/kallsyms.c

4. Lis la doc de `kptr_restrict`:
   ```bash
   cat /proc/sys/kernel/kptr_restrict
   ```
   Si la valeur est 1 ou 2, les adresses sont masquees pour les non-root.

## L'exercice

Pas de code C dans cet exercice. Tu vas explorer la table des symboles
et repondre a des questions dans `answers.txt`.

Commandes a executer dans la VM (en root):

```bash
# Voir les 20 premiers symboles
head -20 /proc/kallsyms

# Chercher commit_creds (on en aura besoin pour l'escalation de privileges)
grep " commit_creds" /proc/kallsyms

# Chercher prepare_kernel_cred (l'autre fonction cle)
grep " prepare_kernel_cred" /proc/kallsyms

# Compter combien de symboles contiennent "sys_"
grep -c "sys_" /proc/kallsyms

# Chercher la sys_call_table
grep " sys_call_table" /proc/kallsyms
```

Cree un fichier `answers.txt` dans ce dossier avec les reponses:

```
1. Adresse de commit_creds: <adresse hex>
2. Adresse de prepare_kernel_cred: <adresse hex>
3. Nombre de symboles contenant "sys_": <nombre>
4. Adresse de sys_call_table: <adresse hex>
5. Type du symbole commit_creds (T, t, D, etc.): <lettre>
```

## Test

```bash
./test.sh
```

Le test verifie que ton fichier `answers.txt` existe et contient les 5 reponses.
