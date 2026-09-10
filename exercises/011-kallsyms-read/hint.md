# Hints, exercice 011

## Niveau 1

Les adresses dans /proc/kallsyms changent a chaque boot (KASLR).
Lance les commandes DANS la VM, pas sur l'hote.

## Niveau 2

Si les adresses sont toutes a 0, tu n'es pas root.
Fais `sudo su` ou boot la VM avec `--user root`.
Verifie aussi: `cat /proc/sys/kernel/kptr_restrict` (doit etre 0).

## Niveau 3

```bash
grep " commit_creds" /proc/kallsyms | awk '{print $1}'
grep " prepare_kernel_cred" /proc/kallsyms | awk '{print $1}'
grep -c "sys_" /proc/kallsyms
grep " sys_call_table" /proc/kallsyms | awk '{print $1}'
grep " commit_creds" /proc/kallsyms | awk '{print $2}'
```
Copie les resultats dans answers.txt, une reponse par ligne.
