# Hints: 003 module-params

## Niveau 1

module_param prend 3 arguments: le nom de la variable, son type, et les
permissions. La variable doit exister comme variable globale avant l'appel.

## Niveau 2

```c
module_param(myint, int, 0444);
module_param(mystr, charp, 0444);
```
`charp` signifie "char pointer", c'est le type pour les chaines.

## Niveau 3

```c
module_param(myint, int, 0444);
module_param(mystr, charp, 0444);
MODULE_PARM_DESC(myint, "un entier de test");
MODULE_PARM_DESC(mystr, "une chaine de test");
```
