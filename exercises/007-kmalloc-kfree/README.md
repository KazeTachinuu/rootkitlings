# Exercice 007: Memoire kernel avec kmalloc/kfree

Mode VM: vng (boot rapide)

## Le mecanisme

En espace utilisateur, tu utilises malloc/free. En kernel, c'est
`kmalloc`/`kfree`. Le kernel a son propre allocateur memoire (SLAB/SLUB)
qui fonctionne differemment de la glibc.

Les fonctions sont declarees dans `include/linux/slab.h`:

https://elixir.bootlin.com/linux/latest/source/include/linux/slab.h

```c
void *kmalloc(size_t size, gfp_t flags);
void kfree(const void *ptr);
```

Le parametre `flags` (GFP = Get Free Pages) controle le comportement de
l'allocation:

- `GFP_KERNEL`: allocation normale, peut dormir (scheduler). A utiliser
  dans la plupart des cas, dans un contexte processus.
- `GFP_ATOMIC`: ne peut PAS dormir. A utiliser dans les handlers
  d'interruption, les spinlocks, les contextes ou le sleeping est interdit.

L'implementation de l'allocateur SLUB est dans `mm/slub.c`:

https://elixir.bootlin.com/linux/latest/source/mm/slub.c

Il y a aussi `kzalloc` (kmalloc + memset a zero) et `krealloc`.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre `include/linux/slab.h` et cherche la definition de kmalloc.
   Note que c'est souvent une inline qui appelle `__kmalloc`.

2. Comprends la difference GFP_KERNEL vs GFP_ATOMIC. Dans un module
   init/exit, on est en contexte processus, donc GFP_KERNEL est correct.

3. kfree(NULL) est valide et ne fait rien (comme free en C). C'est
   une convention kernel.

4. kmalloc retourne NULL si l'allocation echoue. Tu dois TOUJOURS
   verifier le retour.

## L'exercice

Dans `src/memory.c`, complete les TODO:
1. Alloue 256 octets avec kmalloc dans l'init
2. Ecris une chaine dans le buffer alloue
3. Affiche le contenu avec printk
4. Libere le buffer dans l'exit avec kfree

## Test

```bash
./test.sh
```

Le test charge le module, verifie le message dans dmesg, decharge,
et verifie qu'il n'y a pas de warning de fuite memoire.
