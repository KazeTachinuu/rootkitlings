# Exercice 026: hide from lsmod

VM: qemu (./lab/vm.sh)

## Le mecanisme

Quand tu tapes `lsmod`, le programme lit `/proc/modules`. Le kernel construit
ce fichier en iterant une liste chainee doublement liee de struct module.

Le champ qui relie les modules entre eux, dans `include/linux/module.h`:

```c
struct module {
    struct list_head list;   // chaine vers les autres modules
    // ...
};
```

L'ancre de cette liste est la variable `modules` dans `kernel/module/main.c`.
Chaque insmod ajoute un noeud. Chaque rmmod le retire.

Si on retire notre noeud de la liste SANS faire rmmod, le module reste charge
et fonctionnel, mais lsmod ne le voit plus. C'est `list_del`.

`list_del` est definie dans `include/linux/list.h`. C'est une manipulation
de pointeurs prev/next, rien de plus.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre la definition de struct module sur Elixir Bootlin:
   https://elixir.bootlin.com/linux/latest/source/include/linux/module.h

2. Cherche le champ `list` (type `struct list_head`). C'est le lien.

3. Ouvre `list_del` dans include/linux/list.h:
   https://elixir.bootlin.com/linux/latest/source/include/linux/list.h

4. Comprends que list_del(entry) fait:
   - entry->prev->next = entry->next
   - entry->next->prev = entry->prev
   Le noeud est deconnecte mais pas libere.

5. `THIS_MODULE` est une macro qui pointe vers le struct module du module courant.

## L'exercice

Dans `src/hide.c`, complete `hide_init` pour retirer le module de la liste.
C'est 1 ligne de code. Si tu as compris list_del et THIS_MODULE, tu sais quoi ecrire.

Compile avec `make`, puis lance `test.sh` dans la VM.

Note: apres list_del, rmmod risque de ne plus trouver le module (il cherche
dans la meme liste). C'est normal. L'exercice 028 regle ce probleme.

## Test

```bash
./test.sh
```
