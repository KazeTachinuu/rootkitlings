# Deepdive, exercice 013

## Taille de task_struct

Cherche la taille de struct task_struct sur ton kernel:
```bash
pahole -C task_struct /usr/lib/debug/boot/vmlinux-$(uname -r) 2>/dev/null | tail -1
```
(Si pahole n'est pas dispo, estime en comptant les champs sur Elixir Bootlin.)
Sur un kernel 6.x, c'est typiquement entre 7 et 10 Ko par processus.

Questions:
1. Combien de memoire kernel est utilisee juste pour les task_struct si
   tu as 500 processus? Et 50 000 (un serveur charge)?
2. La macro `current` sur x86_64 utilise le segment GS pour un acces per-CPU.
   Pourquoi ne pas simplement stocker current dans une variable globale?
   (hint: pense SMP, plusieurs CPUs executent du code kernel en parallele)
3. Explore `for_each_process(p)` dans include/linux/sched/signal.h.
   C'est une macro qui itere sur TOUS les processus. Comment est-elle
   construite? (hint: c'est une liste chainee via task_struct->tasks)

## Lien avec la suite

A l'exercice 014, tu vas lire les credentials (uid/gid) depuis task_struct.
C'est la base pour la privilege escalation: si tu peux ECRIRE dans les creds,
tu deviens root.
