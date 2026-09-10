# Hints, exercice 043

## Niveau 1

kernel_sendmsg prend un msghdr vide (pour TCP, l'adresse est dans le socket),
un kvec qui pointe vers tes donnees, le nombre de kvec (1), et la taille totale.

## Niveau 2

```c
struct kvec vec;
vec.iov_base = (void *)buf;
vec.iov_len = len;
```

Le msghdr peut etre initialise a zero: `struct msghdr msg = {0};`

## Niveau 3

```c
static int send_message(struct socket *sock, const char *buf, size_t len)
{
    struct msghdr msg = {0};
    struct kvec vec;
    vec.iov_base = (void *)buf;
    vec.iov_len = len;
    return kernel_sendmsg(sock, &msg, &vec, 1, len);
}
```
