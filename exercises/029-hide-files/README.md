# Exercice 029: hide files from ls

VM: qemu (./lab/vm.sh)

## Le mecanisme

Quand tu tapes `ls`, le programme appelle le syscall `getdents64` (get
directory entries). Le kernel remplit un buffer avec des structures
`linux_dirent64`, une par fichier/dossier dans le repertoire.

La structure (dans `include/linux/dirent.h`):

```c
struct linux_dirent64 {
    u64    d_ino;      // inode number
    s64    d_off;      // offset to next
    unsigned short d_reclen;  // taille de CETTE entree
    unsigned char  d_type;    // type (fichier, dir, ...)
    char   d_name[];   // nom du fichier (variable length)
};
```

Les entrees sont empaquetees les unes apres les autres dans le buffer.
La taille totale du buffer est la valeur de retour du syscall.

Pour cacher un fichier: on hooke getdents64, on appelle l'original, puis
on parcourt le buffer retourne. Si une entree a un nom qui commence par
notre prefixe magic ("rootkit_"), on la retire du buffer en decalant les
entrees suivantes et en reduisant la taille de retour.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Lis la definition de getdents64 dans `fs/readdir.c`:
   https://elixir.bootlin.com/linux/latest/source/fs/readdir.c

2. Comprends le layout du buffer: les dirent sont empaquetes, chaque
   entree fait `d_reclen` octets. Pour passer a la suivante:
   `next = (void *)current + current->d_reclen`

3. Pour "supprimer" une entree du milieu, on a deux options:
   a) Ajouter d_reclen a l'entree precedente (elle "absorbe" l'espace)
   b) memmove les entrees suivantes et reduire le total

   L'option (a) est plus simple et c'est celle utilisee en pratique.

4. Le hook utilise ftrace_helper.h (fourni dans src/). Tu as vu comment
   ca marche dans les exercices 019-021.

## L'exercice

Dans `src/hidefiles.c`, le hook de getdents64 est deja en place. Le
squelette appelle l'original et recupere le buffer. A toi de parcourir
les entrees et de retirer celles dont le nom commence par "rootkit_".

Le TODO est dans la boucle de filtrage. Tu dois:
1. Parcourir les entrees avec d_reclen
2. Comparer d_name avec le prefixe
3. Si match, absorber l'entree dans la precedente

Compile avec `make`, puis lance `test.sh` dans la VM.

## Test

```bash
./test.sh
```
