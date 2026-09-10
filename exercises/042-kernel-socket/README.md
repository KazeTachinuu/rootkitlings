# Exercice 042: creer un socket TCP depuis le kernel

VM: qemu (./lab/vm.sh)

## Le mecanisme

Le kernel Linux a sa propre API socket, distincte de celle du userspace.
Pas de socket(), connect(), send() comme en C userland. A la place:

```c
sock_create_kern(net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
kernel_connect(sock, (struct sockaddr *)&addr, sizeof(addr), 0);
```

Les fonctions cles sont dans `net/socket.c`:
- https://elixir.bootlin.com/linux/latest/source/net/socket.c

Les structures:
- `struct socket` (include/linux/net.h): le socket kernel
- `struct sockaddr_in` (include/uapi/linux/in.h): adresse IPv4

Pour obtenir le namespace reseau courant, on utilise `&init_net`
(defini dans `include/net/net_namespace.h`).

## Ce qu'il faut comprendre AVANT d'ecrire

1. Regarde le prototype de sock_create_kern:
   https://elixir.bootlin.com/linux/latest/source/net/socket.c
   Cherche "sock_create_kern".

2. Regarde kernel_connect dans le meme fichier.

3. Comprends struct sockaddr_in:
   ```c
   struct sockaddr_in {
       sa_family_t    sin_family;  // AF_INET
       __be16         sin_port;    // htons(PORT)
       struct in_addr sin_addr;    // ip
   };
   ```

4. Lance un serveur d'ecoute dans la VM:
   ```bash
   nc -l -p 4444 &
   ```
   C'est la cible de connexion de ton module.

## L'exercice

Dans `src/ksock.c`, complete `ksock_init` pour:
1. Creer un socket TCP avec sock_create_kern
2. Remplir une struct sockaddr_in (127.0.0.1, port 4444)
3. Se connecter avec kernel_connect

Le module se connecte a l'init, puis ferme le socket a l'exit.

## Test

```bash
./test.sh
```

Le test lance nc en ecoute, charge le module, verifie que nc a recu une connexion.
