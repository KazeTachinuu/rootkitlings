# Deepdive 034

Tu as lu les credentials du processus courant. Quelques questions:

1. Quelle est la difference entre uid et euid? Pourquoi le kernel
   maintient-il les deux? (Indice: pense a setuid binaries comme sudo.)

2. Le champ `cap_effective` dans struct cred contient les capabilities
   Linux. C'est un systeme plus fin que uid=0. Un processus peut avoir
   CAP_NET_ADMIN sans etre root. Lis `include/linux/capability.h` et
   liste les capabilities les plus dangereuses pour un rootkit.

3. `current_cred()` est RCU-protected. Que se passe-t-il si tu gardes
   le pointeur et que les credentials changent entre temps (par exemple
   si le processus fait exec d'un setuid binary)?
   Lis les commentaires dans `include/linux/cred.h` sur le RCU.

L'exercice 035 va MODIFIER ces credentials pour donner root a un processus.
