# Exercice 028: unhide module for clean rmmod

VM: qemu (./lab/vm.sh)

## Le mecanisme

Les exercices 026 et 027 cachent le module, mais rmmod ne fonctionne plus.
Pourquoi? Parce que rmmod cherche le module dans la liste `modules`. Si le
noeud est deconnecte, rmmod ne le trouve pas.

La solution: sauvegarder les pointeurs AVANT de faire list_del, puis les
restaurer dans module_exit AVANT que rmmod ne cherche a liberer le module.

Comment ca marche concretement:

```c
struct list_head *saved_prev;
// Avant list_del:
saved_prev = THIS_MODULE->list.prev;
// Apres list_del, le module est cache.
// Dans exit, pour re-linker:
list_add(&THIS_MODULE->list, saved_prev);
```

`list_add` insere un noeud juste apres le noeud donne. En re-inserant
apres saved_prev, on remet le module a sa place originale.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Relis `list_del` et `list_add` dans `include/linux/list.h`:
   https://elixir.bootlin.com/linux/latest/source/include/linux/list.h

2. Comprends la difference:
   - list_del(x): deconnecte x de la liste
   - list_add(x, head): insere x juste apres head

3. Le module_exit est appele par rmmod. Si on re-link le module dans exit,
   rmmod le trouve et fait son cleanup normal.

4. Meme logique pour le kobject: on ne fait PAS kobject_del ici (exercice
   simplifie). Cacher le kobject ET le restaurer proprement est plus complexe.

## L'exercice

Dans `src/unhide.c`:
1. Sauvegarde le pointeur prev AVANT de cacher
2. Fais list_del pour cacher
3. Dans module_exit, fais list_add pour restaurer

Le squelette a les variables globales, les TODO marquent ou ecrire.

Compile avec `make`, puis lance `test.sh` dans la VM.

## Test

```bash
./test.sh
```
