# Hints, exercice 027

## Niveau 1

`kobject_del` prend un pointeur vers un `struct kobject`.
Le kobject du module est accessible via `THIS_MODULE->mkobj`.

## Niveau 2

Le champ exact est `THIS_MODULE->mkobj.kobj`.
`kobject_del` veut `&THIS_MODULE->mkobj.kobj`.

## Niveau 3

```c
kobject_del(&THIS_MODULE->mkobj.kobj);
```
