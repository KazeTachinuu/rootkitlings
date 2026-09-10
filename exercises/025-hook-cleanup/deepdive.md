# Deepdive, exercice 025

## RCU en profondeur

RCU est un des mecanismes de synchronisation les plus importants du kernel.
Il est utilise pour les tables de routage, les listes de modules, les creds,
et des centaines d'autres structures.

Le principe: les lecteurs n'ont aucun cout (pas de lock, pas d'atomique).
Les ecrivains font le travail lourd: ils copient, modifient la copie,
remplacent le pointeur atomiquement, puis attendent (synchronize_rcu)
que tous les lecteurs de l'ancienne version aient fini.

Questions:
1. Combien de temps prend synchronize_rcu() sur ta VM?
   Mesure avec ktime_get_ns() avant et apres.
2. Existe-t-il une version non-bloquante? Cherche call_rcu() dans
   include/linux/rcupdate.h. Elle prend un callback appele quand
   la grace period est terminee.
3. Certains rootkits ne font PAS de synchronize_rcu au cleanup.
   Pourquoi "ca marche quand meme" sur la plupart des machines?
   (hint: si rmmod est le seul processus et qu'il bloque dans le
   kernel pendant le cleanup, les autres CPUs passent naturellement
   par un quiescent state)

## Lien avec la suite

Phase 3 terminee. Tu sais maintenant hooker n'importe quelle fonction
kernel avec ftrace. A partir de l'exercice 026, on utilise ces hooks
pour disparaitre: cacher le module, les fichiers, les processus.
