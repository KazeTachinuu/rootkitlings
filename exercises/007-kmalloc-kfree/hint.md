# Hints: 007 kmalloc-kfree

## Niveau 1

kmalloc retourne un void*. N'oublie pas de verifier que le retour
n'est pas NULL avant d'utiliser le buffer.

## Niveau 2

```c
buf = kmalloc(BUF_SIZE, GFP_KERNEL);
if (!buf)
    return -ENOMEM;
```

## Niveau 3

Dans init:
```c
buf = kmalloc(BUF_SIZE, GFP_KERNEL);
if (!buf)
    return -ENOMEM;
```

Dans exit:
```c
kfree(buf);
```
