# Deepdive 043: kernel_sendmsg

## Question ouverte

kernel_sendmsg est bloquant par defaut. Si le serveur ne lit pas, le buffer
TCP se remplit et le kernel bloque. C'est un probleme si on envoie depuis
module_init ou un kthread.

Comment rendre le socket non-bloquant ? Cherche MSG_DONTWAIT dans
include/linux/socket.h et net/socket.c.

Autre approche: utiliser kernel_sendmsg depuis un kthread (exo 010) avec
un timeout sur le socket. Regarde sock_setsockopt et SO_SNDTIMEO.

L'exercice 044 ajoute la reception: kernel_recvmsg.
