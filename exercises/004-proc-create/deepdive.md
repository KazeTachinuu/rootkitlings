# Deepdive: 004 proc-create

1. /proc est monte en tant que procfs. Regarde `fs/proc/root.c` sur Elixir
   Bootlin pour voir comment le filesystem est initialise au boot.

2. Beaucoup de rootkits utilisent /proc comme interface de commande.
   Par exemple, ecrire "hide PID" dans /proc/rootkitlings pour cacher
   un processus. C'est ce qu'on fera aux exos 005 et 006 (read/write).

3. Compare `proc_ops` (kernel 5.6+) avec `file_operations` (ancien).
   Cherche le commit qui a introduit proc_ops. Pourquoi le kernel a-t-il
   separe les deux structures ?
