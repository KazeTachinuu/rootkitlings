# Hints, exercice 042

## Niveau 1

sock_create_kern prend 5 arguments: namespace, family, type, protocol, pointeur.
kernel_connect prend le socket, une sockaddr castee, sa taille, et des flags (0).

## Niveau 2

Pour le socket:
```c
ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
```

Pour l'adresse, utilise in_aton() pour convertir l'IP string en entier.

## Niveau 3

```c
ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
if (ret < 0) { ... }

memset(&addr, 0, sizeof(addr));
addr.sin_family = AF_INET;
addr.sin_port = htons(SERVER_PORT);
addr.sin_addr.s_addr = in_aton(SERVER_IP);
ret = kernel_connect(sock, (struct sockaddr *)&addr, sizeof(addr), 0);
```
