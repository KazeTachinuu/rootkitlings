# Exercice 015: le chemin d'un syscall

## Le mecanisme

Quand un programme userspace appelle `open("/etc/passwd", O_RDONLY)`, voici
le chemin complet jusqu'au kernel:

1. La libc (glibc/musl) traduit `open()` en instruction `syscall` avec
   le numero du syscall dans le registre RAX.

2. L'instruction `syscall` sur x86_64 fait un saut vers `entry_SYSCALL_64`
   dans `arch/x86/entry/entry_64.S`. Ce code sauvegarde les registres
   dans une struct pt_regs sur la stack kernel.

3. `entry_SYSCALL_64` appelle `do_syscall_64` dans
   `arch/x86/entry/common.c`, qui lit le numero de syscall et indexe
   dans `sys_call_table`.

4. `sys_call_table` est un tableau de pointeurs de fonctions, indexe par
   le numero de syscall. La table est definie dans
   `arch/x86/entry/syscall_64.c` et remplie a partir de
   `arch/x86/entry/syscalls/syscall_64.tbl`.

5. Pour open, le handler est `__x64_sys_openat` (wrapper SYSCALL_DEFINE),
   qui appelle `do_sys_openat2` dans `fs/open.c`, qui fait le vrai travail
   (resolution de chemin, creation du struct file, etc.).

6. Le retour remonte la meme chaine, le resultat passe dans RAX.

Les fichiers cles:
- `arch/x86/entry/entry_64.S` : point d'entree assembleur
  https://elixir.bootlin.com/linux/latest/source/arch/x86/entry/entry_64.S
- `arch/x86/entry/common.c` : do_syscall_64
  https://elixir.bootlin.com/linux/latest/source/arch/x86/entry/common.c
- `arch/x86/entry/syscalls/syscall_64.tbl` : table des numeros
  https://elixir.bootlin.com/linux/latest/source/arch/x86/entry/syscalls/syscall_64.tbl
- `fs/open.c` : implementation de openat
  https://elixir.bootlin.com/linux/latest/source/fs/open.c

## Ce qu'il faut comprendre AVANT de chercher

1. Ouvre `entry_SYSCALL_64` dans entry_64.S sur Elixir Bootlin.
   Cherche comment les registres sont sauvegardes dans pt_regs.

2. Ouvre `do_syscall_64` dans common.c. Trouve la ligne qui indexe
   dans sys_call_table.

3. Ouvre `syscall_64.tbl`. Cherche le numero de `openat` (c'est 257).
   Note aussi `getdents64` (217), on en aura besoin pour le file hiding.

4. Dans `fs/open.c`, cherche `SYSCALL_DEFINE` pour openat. Note comment
   la macro SYSCALL_DEFINE genere le wrapper `__x64_sys_openat`.

## L'exercice

Pas de code C. Trace le chemin du syscall `getdents64` (celui qu'on hookera
pour cacher des fichiers) et remplis `answers.txt`:

```
1. Numero de getdents64 dans syscall_64.tbl: <numero>
2. Nom du wrapper genere par SYSCALL_DEFINE: <__x64_sys_...>
3. Fichier source de l'implementation: <fs/...>
4. Fonction interne appelee par le wrapper: <nom>
5. Structure utilisee pour passer les entrees de repertoire: <nom de struct>
```

## Test

```bash
./test.sh
```
