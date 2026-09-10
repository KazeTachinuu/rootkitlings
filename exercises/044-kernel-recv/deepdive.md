# Deepdive 044: kernel_recvmsg

## Question ouverte

kernel_recvmsg en mode bloquant (flags=0) bloque le thread appelant jusqu'a
reception. Dans module_init, ca gele insmod. Dans un kthread, ca gele le thread.

Comment implementer un timeout sur le recv ?

Deux approches:
1. SO_RCVTIMEO via kernel_setsockopt (timeout sur le socket)
2. MSG_DONTWAIT (retourne -EAGAIN si rien a lire, puis sleep manuellement)

Cherche comment KoviD (rootkit open source) gere ses timeouts de connexion.
Regarde src/sock.c dans le depot GitHub carloslack/KoviD.

L'exercice 045 definit un protocole structure pour les echanges.
