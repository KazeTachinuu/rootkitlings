# Deepdive 042: kernel sockets

## Question ouverte

Le socket que tu viens de creer est visible dans `ss -tnp` ou `netstat -tnp`.
La colonne "PID/Program name" sera vide (c'est un socket kernel, pas userspace)
mais la connexion est visible.

Comment un defenseur pourrait-il detecter les sockets kernel suspects ?
Comment un rootkit cacherait-il cette connexion ? (indice: exo 032)

Autre question: pourquoi utilise-t-on &init_net et pas un autre namespace ?
Lis include/net/net_namespace.h et regarde comment les containers (Docker)
utilisent des network namespaces separes. Que se passerait-il si le rootkit
tourne dans un container ?

L'exercice 043 ajoute l'envoi de donnees via ce socket.
