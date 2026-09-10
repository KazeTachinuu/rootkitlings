# Exercice 033: hide a line in a file

VM: qemu (./lab/vm.sh)

## Le mecanisme

Le projet WLKOM demande de "hide a line in a file". Scenario: le rootkit
ajoute une ligne dans /etc/modules-load.d/ pour la persistence, et veut
cacher cette ligne quand quelqu'un cat le fichier.

Pour lire un fichier, le userspace appelle le syscall `read` (ou
`pread64`). Cote kernel, ca passe par `vfs_read` dans `fs/read_write.c`.

Approche: hooker `vfs_read`. Apres l'appel original, si le fichier lu
est notre cible, on parcourt le buffer retourne et on supprime la ligne
qui contient notre marqueur.

Comment savoir quel fichier est lu? Le premier argument de vfs_read est
un `struct file *`. On peut lire le chemin via `file->f_path`:

```c
char *pathname, *p;
pathname = kmalloc(PATH_MAX, GFP_KERNEL);
p = d_path(&file->f_path, pathname, PATH_MAX);
```

Si `p` correspond a notre fichier cible, on filtre le buffer.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Lis `vfs_read` dans `fs/read_write.c`:
   https://elixir.bootlin.com/linux/latest/source/fs/read_write.c

2. La signature:
   ```c
   ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
   ```

3. Apres l'appel original, `buf` contient les donnees lues (en userspace).
   On doit copy_from_user, modifier, copy_to_user.

4. Pour retirer une ligne: chercher le debut de la ligne (strstr sur le
   marqueur), trouver le \n suivant, memmove le reste du buffer.

## L'exercice

Dans `src/hideline.c`, le hook de vfs_read est en place. A toi de:
1. Verifier que le fichier correspond a TARGET_FILE
2. Copier le buffer en kernel space
3. Chercher la ligne contenant HIDE_MARKER
4. La retirer (memmove)
5. Recopier en userspace et ajuster la valeur de retour

10 lignes de code dans la zone TODO.

Compile avec `make`, puis lance `test.sh` dans la VM.

## Test

```bash
./test.sh
```
