# Exercice 008: Listes chainees kernel

Mode VM: vng (boot rapide)

## Le mecanisme

Le kernel Linux n'utilise PAS de listes chainees classiques (ou le noeud
contient un pointeur vers les donnees). Il utilise un modele inverse:
la liste est DANS la structure de donnees.

C'est le pattern `struct list_head`, defini dans `include/linux/list.h`:

https://elixir.bootlin.com/linux/latest/source/include/linux/list.h

```c
struct list_head {
    struct list_head *next, *prev;
};
```

Tu integres ce `list_head` dans ta propre structure:

```c
struct mon_truc {
    int data;
    struct list_head list;  // le lien
};
```

Pour retrouver ta structure a partir du list_head, tu utilises la macro
`container_of` (via `list_entry` ou `list_for_each_entry`). C'est l'une
des macros les plus importantes du kernel.

Cette approche est partout dans le kernel: la liste des modules
(THIS_MODULE->list), la liste des processus (tasks), les files d'attente,
etc. Quand on cachera le module de lsmod a l'exo 026, c'est une
manipulation de list_head.

L'ancre de la liste est un list_head "vide" qui sert de tete:

```c
LIST_HEAD(ma_liste);  // declare et initialise la tete
```

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre `include/linux/list.h` sur Elixir Bootlin. C'est un fichier
   relativement court et tres bien ecrit. Lis les fonctions:
   - list_add / list_add_tail
   - list_del
   - list_for_each_entry
   - INIT_LIST_HEAD / LIST_HEAD

2. Comprends `container_of` dans `include/linux/container_of.h`:
   https://elixir.bootlin.com/linux/latest/source/include/linux/container_of.h
   Cette macro prend un pointeur vers un membre d'une structure et retourne
   un pointeur vers la structure englobante. C'est du C pur (offset calcul).

3. Pourquoi ce design ? Parce qu'une meme structure peut etre dans
   PLUSIEURS listes simultanement (en ayant plusieurs list_head).

## L'exercice

Dans `src/lists.c`, on definit une struct entry avec un int value et un
list_head. Complete les TODO pour:
1. Declarer la tete de liste avec LIST_HEAD
2. Dans l'init, creer 3 entries (kmalloc), initialiser leur value,
   et les ajouter a la liste avec list_add_tail
3. Iterer sur la liste avec list_for_each_entry et printk chaque value
4. Dans l'exit, liberer toutes les entries avec list_for_each_entry_safe

## Test

```bash
./test.sh
```

Le test verifie que dmesg contient les 3 valeurs.
