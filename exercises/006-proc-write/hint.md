# Hints: 006 proc-write

## Niveau 1

La signature de la fonction write prend un `const char __user *buf`.
Le `__user` signifie que tu ne peux PAS lire buf directement.
Tu dois passer par copy_from_user.

## Niveau 2

```c
static ssize_t rk_write(struct file *file, const char __user *buf,
                        size_t count, loff_t *ppos)
{
    if (count >= BUF_SIZE)
        count = BUF_SIZE - 1;
    if (copy_from_user(msg_buf, buf, count))
        return -EFAULT;
    // termine la chaine et retourne count
}
```

## Niveau 3

```c
static ssize_t rk_write(struct file *file, const char __user *buf,
                        size_t count, loff_t *ppos)
{
    if (count >= BUF_SIZE)
        count = BUF_SIZE - 1;
    if (copy_from_user(msg_buf, buf, count))
        return -EFAULT;
    msg_buf[count] = '\0';
    msg_len = count;
    return count;
}
```
