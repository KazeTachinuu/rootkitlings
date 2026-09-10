# Hints, exercice 024

## Niveau 1

Le buffer est deja copie en espace kernel (kbuf). Tu dois juste iterer
avec un offset qui avance de d->d_reclen a chaque iteration.

## Niveau 2

```c
offset = 0;
while (offset < nread) {
    d = (struct linux_dirent64 *)((char *)kbuf + offset);
    printk(KERN_INFO "rootkitlings/024: entry: %s\n", d->d_name);
    offset += d->d_reclen;
}
```

## Niveau 3

Place ce code juste apres le commentaire TODO, avant le kfree(kbuf).
Le cast `(char *)kbuf + offset` est necessaire car kbuf est un pointeur
vers struct linux_dirent64, et l'arithmetique de pointeurs sur struct
n'avancerait pas du bon nombre d'octets.
