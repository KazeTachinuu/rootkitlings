# Deepdive 030

Le test montre un point important: `cat /tmp/.rootkit_lair/plans.txt`
fonctionne toujours. Le hook getdents64 cache l'entree du repertoire
dans le listing, mais l'acces direct par chemin complet utilise un
autre syscall (open/openat), qui n'est pas hooke.

Questions:

1. Pour cacher aussi l'acces direct, il faudrait hooker openat/open.
   Mais c'est dangereux: tous les programmes utilisent open. Quel serait
   le risque de hooker openat pour refuser l'acces a un chemin?

2. Les rootkits reels combinent souvent hide-from-listing avec un mecanisme
   de "magic knock". Par exemple: seul un processus qui a ecrit un mot de
   passe dans /proc/rootkit_auth peut acceder aux fichiers caches.
   Comment implementerais-tu ca? (Indice: chaque processus a un PID, et
   tu peux maintenir une liste de PIDs autorises.)

3. Lis le code de KoviD (github.com/carloslack/KoviD), fichier
   `src/fs.c`. Comment cache-t-il les repertoires? Utilise-t-il la
   meme technique que toi?

L'exercice 031 applique cette technique a /proc pour cacher des processus.
