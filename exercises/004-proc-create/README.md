# Exercice 004: Creer une entree /proc

Mode VM: vng (boot rapide)

## Le mecanisme

Le filesystem /proc est un filesystem virtuel. Il n'existe pas sur disque.
Chaque fichier dans /proc est genere dynamiquement par le kernel quand tu
le lis. C'est LE mecanisme principal par lequel le kernel expose des infos
au userspace.

La creation d'entrees /proc depuis un module se fait via `proc_create()`,
definie dans `include/linux/proc_fs.h`:

https://elixir.bootlin.com/linux/latest/source/include/linux/proc_fs.h

```c
struct proc_dir_entry *proc_create(
    const char *name,           // nom du fichier dans /proc
    umode_t mode,               // permissions (0444 = read-only)
    struct proc_dir_entry *parent, // NULL = directement dans /proc
    const struct proc_ops *proc_ops // les operations (read, write, etc.)
);
```

Les operations sont definies dans `struct proc_ops` (attention: dans les
kernels recents, c'est `proc_ops`, pas `file_operations`):

https://elixir.bootlin.com/linux/latest/source/include/linux/proc_fs.h

Pour supprimer l'entree au dechargement, on utilise `proc_remove()` ou
`remove_proc_entry()`.

L'implementation se trouve dans `fs/proc/generic.c`:

https://elixir.bootlin.com/linux/latest/source/fs/proc/generic.c

## Ce qu'il faut comprendre AVANT d'ecrire

1. Liste le contenu de /proc sur ta machine:
   ```bash
   ls /proc/
   ```
   Chaque numero est un processus. Les fichiers nommes (cpuinfo, meminfo,
   modules, etc.) sont des entrees creees par le kernel.

2. Lis /proc/modules:
   ```bash
   cat /proc/modules
   ```
   C'est ce que lsmod affiche. Quand on cachera le module plus tard,
   c'est cette liste qu'on manipulera.

3. Ouvre Elixir Bootlin et cherche `struct proc_ops`. Note les champs:
   proc_read, proc_write, proc_open, etc.

## L'exercice

Dans `src/myproc.c`, complete les TODO pour:
1. Declarer une struct proc_ops avec un proc_open et un proc_read
2. Appeler proc_create dans l'init
3. Appeler proc_remove dans l'exit

On utilise `single_open` / `single_release` pour simplifier (seq_file).
La fonction show qui genere le contenu est deja ecrite.

## Test

```bash
./test.sh
```

Le test verifie que /proc/rootkitlings existe et contient le bon texte.
