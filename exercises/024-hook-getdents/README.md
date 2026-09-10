# Exercice 024: hooker getdents64, comprendre les directory entries

## Le mecanisme

Quand tu tapes `ls`, la commande appelle le syscall `getdents64` pour lire
les entrees d'un repertoire. Le kernel remplit un buffer avec des structures
`linux_dirent64`:

```c
struct linux_dirent64 {
    u64        d_ino;    // numero d'inode
    s64        d_off;    // offset vers la prochaine entree
    unsigned short d_reclen;  // taille de cette entree (variable)
    unsigned char  d_type;    // type (fichier, dir, symlink, etc.)
    char       d_name[]; // nom du fichier (taille variable, null-terminated)
};
```

Le buffer retourne par getdents64 est un tableau contigu de ces structures,
chacune avec une taille variable (d_reclen). Pour iterer:

```
ptr = buffer;
while (ptr < buffer + nread) {
    struct linux_dirent64 *d = (struct linux_dirent64 *)ptr;
    // d->d_name est le nom du fichier
    ptr += d->d_reclen;
}
```

Le syscall retourne le nombre total d'octets dans le buffer (nread).
Pour cacher un fichier (exercice 029), il suffit de retirer son entree
du buffer et d'ajuster nread. Mais d'abord, on doit comprendre le buffer.

Implementation dans:
- `fs/readdir.c` : getdents64, iterate_dir, filldir64
  https://elixir.bootlin.com/linux/latest/source/fs/readdir.c

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre `fs/readdir.c` sur Elixir Bootlin et cherche `SYSCALL_DEFINE3(getdents64`.
2. Regarde comment `iterate_dir` remplit le buffer via `filldir64`.
3. Le buffer est en espace UTILISATEUR (regs->si = pointeur userspace).
   Pour le lire depuis un hook kernel, il faut le copier avec `copy_from_user`,
   puis le remettre avec `copy_to_user`.
4. Dans cet exercice, on ne modifie pas le buffer. On le lit et on affiche
   les noms. La modification viendra a l'exercice 029.

## L'exercice

Dans `src/hookdents.c`, hooke `__x64_sys_getdents64`. Apres l'appel
a l'original, copie le buffer userspace, itere sur les entrees, et affiche
chaque nom de fichier.

Copie `ftrace_helper.h` depuis l'exercice 021:
```bash
cp ../021-ftrace-helper/src/ftrace_helper.h src/
```

```bash
make
# Dans la VM (qemu):
insmod src/hookdents.ko
ls /tmp
dmesg | tail -30
rmmod hookdents
```

Tu devrais voir tous les noms de fichiers de /tmp dans dmesg.

## Test

```bash
./test.sh
```
