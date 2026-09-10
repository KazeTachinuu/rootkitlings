# Deepdive 031

Tu caches un PID de /proc. Mais `ps` n'est pas le seul outil qui
detecte les processus.

Questions:

1. `top` et `htop` utilisent-ils aussi getdents64 sur /proc? Verifie
   avec `strace top` dans la VM.

2. Notre hook filtre TOUS les appels a getdents64, pas seulement ceux
   sur /proc. C'est inefficace. Comment detecter que le fd pointe vers
   /proc? Indice: `current->files`, `fdget`, `f_path.dentry->d_iname`.
   Implemente cette verification pour ne filtrer que sur /proc.

3. Le rootkit Diamorphine cache les processus en utilisant un signal
   magic (kill -31 PID). Le processus n'est pas tue, le signal est
   intercepte, et le PID est ajoute a une liste de PIDs caches.
   Comment gererais-tu une LISTE dynamique de PIDs au lieu d'un seul?

4. Audit: un administrateur peut detecter un processus cache en lisant
   directement /proc/<PID>/status si il connait le PID. Comment?
   En faisant un scan sequentiel de tous les PIDs possibles (1 a PID_MAX).
   Ecris un script bash qui detecte les PIDs caches par ton module.
