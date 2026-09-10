# Deepdive 046: execution de commandes

## Question ouverte

call_usermodehelper ecrit dans /tmp/rkl_out. Ce fichier est visible par
n'importe quel processus sur la machine. Un defenseur qui surveille les
fichiers dans /tmp le verrait.

Comment transmettre le resultat sans fichier intermediaire ?

Approche 1: utiliser un pipe kernel (comme |) et lire la sortie dans le
module via kernel_read. Cherche filp_open, kernel_read dans le kernel.

Approche 2: ne pas utiliser call_usermodehelper du tout. A la place,
utiliser le sous-systeme kernel pour executer des programmes et capturer
la sortie via un pseudo-terminal (pty). C'est beaucoup plus complexe.

Approche 3 (la plus simple pour WLKOM): rediriger vers un fichier, le lire,
l'envoyer via le socket, puis le supprimer immediatement.

L'exercice 047 combine tout: boucle C2 avec socket, protocole, et execution.
