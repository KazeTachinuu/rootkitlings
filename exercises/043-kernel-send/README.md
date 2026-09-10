# Exercice 043: envoyer des donnees depuis le kernel

VM: qemu (./lab/vm.sh)

## Le mecanisme

Pour envoyer des donnees via un socket kernel, on utilise `kernel_sendmsg`.
C'est l'equivalent kernel de sendmsg(2).

```c
int kernel_sendmsg(struct socket *sock, struct msghdr *msg,
                   struct kvec *vec, size_t num, size_t size);
```

Les structures:
- `struct msghdr`: metadata du message (drapeaux, adresse destination)
- `struct kvec`: le buffer de donnees (comme struct iovec mais pour le kernel)

```c
struct kvec {
    void  *iov_base;  // pointeur vers les donnees
    size_t iov_len;   // taille
};
```

Source:
- https://elixir.bootlin.com/linux/latest/source/net/socket.c
  Cherche "kernel_sendmsg"

## Ce qu'il faut comprendre AVANT d'ecrire

1. Regarde le code de kernel_sendmsg dans net/socket.c:
   il utilise iov_iter_kvec pour wrapper le kvec dans un iov_iter.

2. Le msghdr est simple pour TCP: on n'a pas besoin de remplir l'adresse
   (elle est deja dans le socket connecte). On met juste msg_flags = 0.

3. kernel_sendmsg retourne le nombre d'octets envoyes, ou une erreur negative.

## L'exercice

Dans `src/ksend.c`, le socket est deja cree et connecte (code de l'exo 042
fourni). Complete la fonction `send_message` pour envoyer un message via
kernel_sendmsg.

## Test

```bash
./test.sh
```

Le test lance nc en ecoute, charge le module, et verifie que nc a recu
le message "rootkitlings hello".
