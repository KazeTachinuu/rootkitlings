# Exercice 022: hooker mkdir

## Le mecanisme

Le syscall mkdir est gere par `__x64_sys_mkdir` (ou `__x64_sys_mkdirat`
selon la version du kernel). L'implementation est dans `fs/namei.c`,
fonction `do_mkdirat`.

Le syscall mkdirat(int dfd, const char __user *pathname, umode_t mode):
- dfd : directory file descriptor (AT_FDCWD pour le repertoire courant)
- pathname : chemin du nouveau dossier (en espace utilisateur)
- mode : permissions (ex: 0755)

Quand on hooke le wrapper `__x64_sys_mkdirat`, les arguments sont dans pt_regs:
- regs->di = dfd
- regs->si = pathname (pointeur userspace)
- regs->dx = mode

Pour lire une string userspace depuis le kernel, on utilise `strncpy_from_user`
ou `copy_from_user`. Attention: le pointeur est en espace utilisateur, tu ne
peux PAS le dereferencer directement.

Defini dans:
- `fs/namei.c` : do_mkdirat
  https://elixir.bootlin.com/linux/latest/source/fs/namei.c

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre `fs/namei.c` sur Elixir Bootlin et cherche `do_mkdirat`.
2. Regarde comment `getname` transforme un pointeur userspace en
   `struct filename *` kernel-safe.
3. `strncpy_from_user(dst, src, count)` copie une string depuis l'espace
   utilisateur. Retourne le nombre de chars copies, ou un code d'erreur
   negatif.

## L'exercice

Dans `src/hookmkdir.c`, utilise ftrace_helper.h pour hooker `__x64_sys_mkdirat`.
Le hook doit lire le chemin du dossier et le logger.

Copie `ftrace_helper.h` depuis l'exercice 021:
```bash
cp ../021-ftrace-helper/src/ftrace_helper.h src/
```

```bash
make
# Dans la VM (qemu):
insmod src/hookmkdir.ko
mkdir /tmp/rootkitlings_test
dmesg | tail
rmmod hookmkdir
rmdir /tmp/rootkitlings_test
```

## Test

```bash
./test.sh
```
