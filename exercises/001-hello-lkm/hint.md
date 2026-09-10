# Hints pour le challenge 001

## Niveau 1

Tu as besoin de 3 includes: `linux/module.h`, `linux/init.h`, et c'est tout
pour ce module. Regarde hello.c pour la structure.

## Niveau 2

La structure d'un module minimal:
- une fonction `static int __init` qui retourne 0
- une fonction `static void __exit`
- `module_init()` et `module_exit()` pour les enregistrer
- `MODULE_LICENSE`, `MODULE_AUTHOR`, `MODULE_DESCRIPTION`

`pr_info("texte\n")` est le moyen le plus simple d'afficher.

## Niveau 3

```c
#include <linux/module.h>
#include <linux/init.h>

static int __init challenge_init(void)
{
    pr_info("rootkitlings/001: tonlogin reporting for duty\n");
    return 0;
}

static void __exit challenge_exit(void)
{
    pr_info("rootkitlings/001: tonlogin out\n");
}

module_init(challenge_init);
module_exit(challenge_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("tonlogin");
MODULE_DESCRIPTION("challenge 001");
```
