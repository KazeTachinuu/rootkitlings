# Hints, exercice 025

## Niveau 1

Une seule ligne a ajouter. C'est un appel bloquant qui attend que tous
les lecteurs RCU aient termine.

## Niveau 2

Ajoute cette ligne apres fh_remove_hooks et avant le printk:
```c
synchronize_rcu();
```

## Niveau 3

N'oublie pas l'include `<linux/rcupdate.h>` (deja present dans le fichier).
synchronize_rcu() est bloquant: il attend que tous les CPUs aient passe
par un "quiescent state" (changement de contexte, idle, retour userspace).
Ca peut prendre quelques millisecondes sur une machine chargee.
