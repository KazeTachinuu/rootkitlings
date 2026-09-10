# Hints, exercice 015

## Niveau 1

Ouvre `arch/x86/entry/syscalls/syscall_64.tbl` sur Elixir Bootlin.
Cherche "getdents64" avec Ctrl+F.

## Niveau 2

Reponses partielles:
1. getdents64 = 217
2. Le wrapper suit la convention `__x64_sys_` + nom du syscall
3. L'implementation est dans fs/readdir.c

## Niveau 3

```
1. Numero de getdents64 dans syscall_64.tbl: 217
2. Nom du wrapper genere par SYSCALL_DEFINE: __x64_sys_getdents64
3. Fichier source de l'implementation: fs/readdir.c
4. Fonction interne appelee par le wrapper: iterate_dir
5. Structure utilisee pour passer les entrees de repertoire: linux_dirent64
```
