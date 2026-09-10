# Exercice 044: recevoir des donnees dans le kernel

VM: qemu (./lab/vm.sh)

## Le mecanisme

`kernel_recvmsg` est le pendant de kernel_sendmsg. Il lit des donnees depuis
un socket connecte.

```c
int kernel_recvmsg(struct socket *sock, struct msghdr *msg,
                   struct kvec *vec, size_t num, size_t size, int flags);
```

Meme structure que kernel_sendmsg: un msghdr, un kvec pour le buffer, mais
avec un parametre `flags` en plus (0 pour bloquer, MSG_DONTWAIT pour non-bloquant).

kernel_recvmsg retourne le nombre d'octets lus, 0 si la connexion est fermee,
ou une erreur negative.

Source:
- https://elixir.bootlin.com/linux/latest/source/net/socket.c
  Cherche "kernel_recvmsg"

## Ce qu'il faut comprendre AVANT d'ecrire

1. Regarde kernel_recvmsg dans net/socket.c.

2. Comprends la difference avec kernel_sendmsg: le kvec sert de buffer
   de RECEPTION (le kernel ecrit dedans), pas d'envoi.

3. Par defaut, kernel_recvmsg bloque jusqu'a reception. Pour un rootkit,
   on voudra un timeout ou MSG_DONTWAIT. Ici on utilise le mode bloquant
   car le test envoie immediatement.

## L'exercice

Dans `src/krecv.c`, complete la fonction `recv_message` pour recevoir des
donnees via kernel_recvmsg et les afficher dans dmesg.

Le module se connecte, envoie un hello, attend la reponse du serveur,
et l'affiche dans les logs.

Un serveur de test Python `test_server.py` est fourni: il attend la connexion,
lit le hello, et repond "ack from server".

## Test

```bash
./test.sh
```

Le test lance test_server.py, charge le module, verifie que dmesg contient
la reponse du serveur.
