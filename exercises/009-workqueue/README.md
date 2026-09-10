# Exercice 009: Workqueues

Mode VM: vng (boot rapide)

## Le mecanisme

Un module kernel tourne dans le contexte du processus qui l'a appele
(insmod, un syscall, etc.). Mais parfois tu veux executer du code
plus tard, de facon asynchrone. C'est le role des workqueues.

Une workqueue est une file d'attente de taches. Chaque tache (work)
est une fonction qui sera executee par un thread kernel dedie. Tu peux
planifier une tache pour execution immediate ou apres un delai.

Les workqueues sont definies dans `include/linux/workqueue.h`:

https://elixir.bootlin.com/linux/latest/source/include/linux/workqueue.h

Les concepts cles:
- `struct delayed_work`: une tache avec un timer integre
- `INIT_DELAYED_WORK(&work, callback)`: initialise la tache
- `schedule_delayed_work(&work, delay)`: planifie l'execution
- `cancel_delayed_work_sync(&work)`: annule et attend la fin

Le delai est exprime en jiffies. `msecs_to_jiffies(ms)` convertit des
millisecondes en jiffies.

L'implementation est dans `kernel/workqueue.c`:

https://elixir.bootlin.com/linux/latest/source/kernel/workqueue.c

Un rootkit utilise les workqueues pour des taches periodiques:
beacon C2, scan de processus, verification de persistence, etc.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre `include/linux/workqueue.h` et cherche `INIT_DELAYED_WORK`.
   Note que ca prend un pointeur vers ta struct delayed_work et une
   fonction callback.

2. La callback a la signature `void (*work_func_t)(struct work_struct *)`.
   Note que c'est un `work_struct*`, pas `delayed_work*`.

3. `schedule_delayed_work` est un wrapper autour de `queue_delayed_work`
   qui utilise la workqueue systeme (system_wq). Pour les cas simples,
   c'est suffisant.

## L'exercice

Dans `src/workq.c`, complete les TODO:
1. Initialise le delayed_work dans l'init
2. Planifie l'execution avec un delai de 1 seconde
3. La callback affiche un message et se re-planifie (boucle)
4. Annule le work dans l'exit

## Test

```bash
./test.sh
```

Le test charge le module, attend 3 secondes, verifie que le message
periodique apparait, puis decharge.
