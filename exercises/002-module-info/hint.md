# Hints: 002 module-info

## Niveau 1

Les macros MODULE_LICENSE, MODULE_AUTHOR, etc. se mettent en dehors de
toute fonction, au niveau global du fichier .c. Chacune prend une string.

## Niveau 2

```c
MODULE_LICENSE("GPL");
MODULE_AUTHOR("...");
```
N'oublie pas le point-virgule apres chaque macro.

## Niveau 3

```c
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ton nom");
MODULE_DESCRIPTION("rootkitlings exercice 002");
MODULE_VERSION("0.1");
```
