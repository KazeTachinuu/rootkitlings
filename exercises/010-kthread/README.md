# Exercice 010: Kernel Threads

Mode VM: vng (boot rapide)

## Le mecanisme

Un kernel thread (kthread) est un thread qui tourne entierement en espace
kernel, sans processus utilisateur associe. Tu peux le voir avec `ps aux`
(les processus entre crochets comme [kworker], [ksoftirqd], [migration]).

Les kthreads sont definis dans `include/linux/kthread.h`:

https://elixir.bootlin.com/linux/latest/source/include/linux/kthread.h

L'implementation est dans `kernel/kthread.c`:

https://elixir.bootlin.com/linux/latest/source/kernel/kthread.c

Les fonctions cles:
- `kthread_run(fn, data, name)`: cree et demarre un kthread
- `kthread_should_stop()`: verifie si on a demande l'arret
- `kthread_stop(task)`: demande l'arret et attend la fin

Le pattern standard d'un kthread:

```c
static int my_thread_fn(void *data)
{
    while (!kthread_should_stop()) {
        // ton travail
        ssleep(1);  // dort 1 seconde
    }
    return 0;
}
```

Un rootkit utilise des kthreads pour:
- La boucle C2 (connexion, reception de commandes, execution)
- Le beacon periodique (signaler la presence au serveur)
- Le monitoring (surveiller les processus, etc.)

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre `include/linux/kthread.h` et lis la doc de kthread_run.
   Note que c'est une macro qui appelle kthread_create puis wake_up_process.

2. `kthread_should_stop()` retourne true quand un autre thread a appele
   `kthread_stop()` sur ce kthread. C'est le mecanisme d'arret cooperatif.
   Le kthread doit verifier regulierement et sortir de sa boucle.

3. `ssleep(n)` dort n secondes. C'est un wrapper autour de
   `schedule_timeout`. Pendant ce sleep, le CPU est libre pour
   d'autres taches.

4. `kthread_stop()` est bloquant: il attend que le kthread termine.
   C'est important pour l'exit du module (on ne peut pas decharger
   le module tant que le thread tourne).

## L'exercice

Dans `src/kthread.c`, complete les TODO:
1. Ecris la fonction du thread qui boucle, affiche un compteur, et dort
2. Dans l'init, lance le kthread avec kthread_run
3. Dans l'exit, arrete le kthread avec kthread_stop

## Test

```bash
./test.sh
```

Le test charge le module, attend 3 secondes, verifie les messages
periodiques, puis decharge proprement.
