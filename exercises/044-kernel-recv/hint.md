# Hints, exercice 044

## Niveau 1

kernel_recvmsg a la meme signature que kernel_sendmsg, plus un parametre flags.
Le kvec sert de buffer de reception cette fois (le kernel y ecrit).

## Niveau 2

```c
struct kvec vec;
vec.iov_base = buf;
vec.iov_len = len;
```

Le 6eme parametre (flags) est 0 pour bloquer jusqu'a reception de donnees.

## Niveau 3

```c
static int recv_msg(struct socket *sock, char *buf, size_t len)
{
    struct msghdr msg = {0};
    struct kvec vec;
    vec.iov_base = buf;
    vec.iov_len = len;
    return kernel_recvmsg(sock, &msg, &vec, 1, len, 0);
}
```
