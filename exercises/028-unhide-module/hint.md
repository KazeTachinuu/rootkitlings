# Hints, exercice 028

## Niveau 1

Tu as besoin de 3 operations:
1. Sauvegarder le voisin
2. Deconnecter (list_del)
3. Reconnecter (list_add)

## Niveau 2

Dans init:
```c
saved_prev = THIS_MODULE->list.prev;
list_del(&THIS_MODULE->list);
```
Dans exit, utilise `list_add` pour re-inserer apres `saved_prev`.

## Niveau 3

Init:
```c
saved_prev = THIS_MODULE->list.prev;
list_del(&THIS_MODULE->list);
```

Exit:
```c
list_add(&THIS_MODULE->list, saved_prev);
```
