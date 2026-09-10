# Exercice 031: hide a process from ps

VM: qemu (./lab/vm.sh)

## Le mecanisme

`ps` liste les processus en lisant le contenu de `/proc`. Chaque processus
a un dossier `/proc/<PID>/`. Le programme `ps` fait un getdents64 sur `/proc`
et lit chaque dossier numerique.

Pour cacher un processus, on reutilise le hook getdents64 de l'exercice 029.
La difference: on ne filtre que quand getdents64 est appele sur `/proc`
(pas sur tous les repertoires), et on filtre par PID (un nombre) au lieu
d'un prefixe de nom.

Comment savoir si getdents64 est appele sur /proc?

Le premier argument du syscall est le file descriptor. On peut retrouver
le chemin du fichier ouvert via le `struct file` associe au fd:

```c
struct file *f = fget(fd);
// f->f_path.dentry->d_name.name contient le nom du dossier
```

Pour /proc, le dentry parent est la racine du procfs. On peut aussi
comparer le nom du superblock ou simplement verifier si le nom du
dentry est un nombre (PID).

Approche simplifiee pour cet exercice: on verifie juste si d_name est
un nombre et correspond a notre PID cible.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Lis `fs/proc/base.c` sur Elixir Bootlin:
   https://elixir.bootlin.com/linux/latest/source/fs/proc/base.c
   Cherche `proc_fill_super` et `proc_root_readdir`.

2. Comprends que /proc est un filesystem special (procfs). Les dossiers
   numeriques sont crees dynamiquement pour chaque task_struct existante.

3. `simple_strtol` ou `kstrtol` convertissent une string en nombre.
   Si le d_name est un nombre et correspond a target_pid, on le cache.

## L'exercice

Dans `src/hideproc.c`, complete la boucle de filtrage pour:
1. Verifier si d_name est un nombre (kstrtol)
2. Si oui et si le nombre correspond a target_pid, retirer l'entree

Le PID cible est un module_param.

Compile avec `make`, puis lance `test.sh` dans la VM.

## Test

```bash
./test.sh
```
