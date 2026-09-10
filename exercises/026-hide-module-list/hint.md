# Hints, exercice 026

## Niveau 1

`list_del` prend un pointeur vers un `struct list_head`.
Le champ qui te connecte a la liste des modules est dans `THIS_MODULE`.

## Niveau 2

Le champ s'appelle `list`. Donc `THIS_MODULE->list` est le list_head.
`list_del` veut un pointeur: `&THIS_MODULE->list`.

## Niveau 3

```c
list_del(&THIS_MODULE->list);
```
