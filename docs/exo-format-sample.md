# Format d'un exo, exemple 021-hide-module-list

Un exo est un dossier. Voici la structure et l'exemple.

```
exercises/021-hide-module-list/
  README.md          Le brief: mecanisme, exploration prealable, ce qu'on ecrit
  src/
    hide.c           Le code de depart avec // TODO(rootkitlings)
  Makefile
  test.sh            Verifie que lsmod ne montre plus le module
  hint.md            Aide progressive si bloque
  deepdive.md        Question ouverte pour aller plus loin
```

## README.md d'exemple

---

# Exercice 021, hide from lsmod

## Le mecanisme

Quand tu tapes `lsmod`, l'outil lit `/proc/modules`. Cette entree est construite
par le kernel en iterant sur une liste chainee doublement liee, ancree dans
`struct module`. Chaque module inserse dans le kernel est un noeud de cette liste.

Le champ concerne, dans `include/linux/module.h`:

```c
struct module {
    struct list_head list;   // le lien vers modules
    ...
    struct module_kobject mkobj;
    ...
};
```

L'ancre globale est `modules` dans `kernel/module/main.c` (le fichier a bouge
selon les versions, dans les kernels 6.x c'est `kernel/module/main.c`).

Pour cacher le module de lsmod, on l'unlink de cette liste. Le pointeur reste
en memoire, le module continue de tourner, mais l'iteration lsmod ne le voit plus.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre Elixir Bootlin et regarde `struct module`:
   https://elixir.bootlin.com/linux/latest/source/include/linux/module.h

2. Grep `EXPORT_SYMBOL` pour voir si on a acces a `modules` ou si on doit
   passer par `THIS_MODULE`:
   ```bash
   grep -rn "EXPORT_SYMBOL.*modules" kernel/module/
   ```

3. Lis la fonction `m_show` dans `kernel/module/procfs.c`. C'est CETTE fonction
   qui construit chaque ligne de `/proc/modules`. Elle iterle sur `modules`.

4. Regarde `list_del` dans `include/linux/list.h`. Comprends que c'est une
   simple manipulation de pointeurs prev/next.

## L'exercice

Dans `src/hide.c`, complete `hide_module_init` pour retirer le module courant
de la liste `modules`. Utilise `THIS_MODULE`. Le fichier fourni te donne
l'ossature de module init/exit et un printk. A toi de faire la manip.

```c
static int __init hide_module_init(void) {
    printk(KERN_INFO "rootkitlings/021: hiding from lsmod\n");

    // TODO(rootkitlings): retire THIS_MODULE de la liste modules.
    //                     1 ligne de code.

    return 0;
}
```

## Test

Le test charge le module, verifie qu'il n'apparait pas dans `lsmod`, puis rmmod.

```bash
insmod hide.ko
lsmod | grep -q hide && echo "FAIL still visible" && exit 1
echo "PASS hidden from lsmod"
rmmod hide 2>&1 | tee /tmp/rmmod.log
# Attention: rmmod risque de fail si le module est unlink de la liste.
# L'exo 023 s'occupe du restore avant unload.
```

## Hint progressif (hint.md)

Niveau 1:
> `list_del` prend un pointeur vers `struct list_head`.

Niveau 2:
> Le champ que tu veux delink est `THIS_MODULE->list`.

Niveau 3:
> `list_del(&THIS_MODULE->list);`

## Deepdive (deepdive.md)

Tu as retire le module de la liste `modules`. Mais si tu tapes
`ls /sys/module/`, tu le vois toujours. Pourquoi ?

Va lire `kernel/module/sysfs.c` et cherche ou `module_kobject` est ajoute
au sysfs. Trouve la fonction et explique en 3 lignes pourquoi list_del sur
`->list` ne suffit pas. On regle ca a l'exo 022.

Bonus: avec Volatility, la liste `modules` peut etre reconstruite en scannant
la memoire kernel a la recherche des pool tags. Comment un defenseur peut-il
detecter les modules unlink ? Cherche "module hunting Volatility".

---

## Ce qui compte dans ce format

- Le mecanisme est explique AVANT le code, avec les paths kernel exacts.
- L'etudiant DOIT lire du code kernel avant de taper.
- Le code a ecrire est minuscule (1 ligne ici), mais on ne peut le taper que si on a compris.
- Le hint est progressif (3 niveaux) pour ne pas spoiler.
- Le deepdive ouvre sur la question suivante ET une autre discipline (forensics).
- Le test capture l'echec attendu (rmmod fail) et l'utilise pour justifier l'exo suivant.

Est-ce que ce format te convient ?
