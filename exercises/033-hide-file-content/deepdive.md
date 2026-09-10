# Deepdive 033

Tu caches une ligne dans un fichier. C'est la technique utilisee pour
la persistence furtive: ajouter une entree dans /etc/modules-load.d/ ou
/etc/ld.so.preload, et la cacher de cat/less/grep.

Questions:

1. Notre hook filtre TOUS les appels a vfs_read. C'est tres couteux:
   chaque read() de chaque processus passe par notre hook. Quel impact
   sur les performances? Comment limiter le cout (par exemple, ne filtrer
   que pour certains inodes)?

2. Un editeur de texte comme vim utilise mmap() au lieu de read() pour
   ouvrir les fichiers. Notre hook ne cache pas la ligne pour vim.
   Pour cacher aussi de mmap, il faudrait hooker une autre fonction.
   Laquelle? Lis `mm/filemap.c` sur Elixir Bootlin.

3. Le sujet WLKOM dit "hide a line in a file" et attribue 2 points.
   C'est parce que c'est non trivial: il faut gerer le cas ou la ligne
   est split entre deux appels read() (le buffer fait 4096 octets par
   defaut). Comment gererais-tu ce cas?

Fin de la Phase 4 (Hiding). Tu maitrises les principales techniques
d'invisibilite d'un rootkit. La Phase 5 (Privilege Escalation) commence
a l'exercice 034.
