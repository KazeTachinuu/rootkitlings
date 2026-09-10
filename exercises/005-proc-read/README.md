# Exercice 005: Lire depuis /proc avec seq_file

Mode VM: vng (boot rapide)

## Le mecanisme

A l'exo 004, on a utilise `single_open` et `seq_printf` sans les expliquer.
Maintenant on va comprendre seq_file en detail.

Le probleme: quand le userspace fait `cat /proc/ton_fichier`, le kernel doit
generer le contenu a la volee. Mais read() peut etre appele plusieurs fois
(si le contenu est grand), et le kernel doit garder un etat entre les appels.

seq_file resout ca. C'est une abstraction definie dans `include/linux/seq_file.h`:

https://elixir.bootlin.com/linux/latest/source/include/linux/seq_file.h

L'implementation est dans `fs/seq_file.c`:

https://elixir.bootlin.com/linux/latest/source/fs/seq_file.c

Le modele seq_file utilise 4 callbacks:
- start: demarre l'iteration, retourne le premier element
- next: avance au suivant
- stop: finit l'iteration
- show: affiche un element avec seq_printf

Pour un fichier simple (un seul bloc de texte), `single_open` simplifie tout:
il ne demande qu'une fonction show.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre `include/linux/seq_file.h` et regarde `struct seq_operations`.
   Les 4 callbacks (start, next, stop, show) sont le coeur du mecanisme.

2. Lis le code de `single_open` dans `fs/seq_file.c`. Tu verras qu'il
   cree des seq_operations avec un start/next/stop trivial, et n'utilise
   que ton show.

3. `seq_printf` fonctionne comme printf mais ecrit dans le buffer seq_file.
   Le buffer grandit automatiquement si besoin.

## L'exercice

Dans `src/procread.c`, la structure du module est en place. Le proc_ops
pointe vers une fonction `rk_open` qui utilise `single_open` avec ta
fonction `rk_show`.

Complete le TODO: ecris la fonction `rk_show` qui affiche:
- Le nom du processus qui lit le fichier (current->comm)
- Son PID (current->pid)

## Test

```bash
./test.sh
```

Le test charge le module et verifie que `cat /proc/rootkitlings_read`
affiche le PID et le nom du processus lecteur.
