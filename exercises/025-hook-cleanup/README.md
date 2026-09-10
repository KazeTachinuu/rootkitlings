# Exercice 025: cleanup propre, eviter les panics

## Le mecanisme

Quand tu fais rmmod sur un module qui a des hooks actifs, il faut que le
unhook se fasse proprement. Si un CPU est en train d'executer ton hook_func
au moment ou le module est decharge, le kernel saute dans de la memoire
deja liberee: kernel panic.

Le probleme:
1. CPU 0: appelle do_sys_openat2, ftrace redirige vers hook_func
2. CPU 1: rmmod, unregister_ftrace_function, module_free libere la memoire
3. CPU 0: hook_func continue d'executer du code qui n'existe plus -> crash

La solution: `synchronize_rcu()`. RCU (Read-Copy-Update) garantit que tous
les CPUs ont termine leurs sections critiques en cours. Apres synchronize_rcu(),
on sait que plus aucun CPU n'est dans hook_func.

L'ordre correct pour le cleanup:
1. `unregister_ftrace_function(&ops)` : desactive le hook (les nouveaux appels
   ne seront plus rediriges)
2. `synchronize_rcu()` : attend que les appels en cours se terminent
3. Maintenant c'est safe de decharger le module

Defini dans:
- `include/linux/rcupdate.h` : synchronize_rcu
  https://elixir.bootlin.com/linux/latest/source/include/linux/rcupdate.h
- `kernel/rcu/` : implementation de RCU
  https://elixir.bootlin.com/linux/latest/source/kernel/rcu/

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre include/linux/rcupdate.h et cherche synchronize_rcu().
2. RCU est un mecanisme de synchronisation lock-free utilise massivement
   dans le kernel. Il permet a des lecteurs de lire des donnees sans lock,
   tandis que les ecrivains attendent que tous les lecteurs aient fini.
3. ftrace_helper.h de l'exercice 021 ne fait PAS de synchronize_rcu.
   C'est un oubli courant dans les rootkits (et meme dans certains exemples).

## L'exercice

Dans `src/cleanup.c`, un hook est installe mais le cleanup dans module_exit
est volontairement incomplet (pas de synchronize_rcu). Complete-le.

Copie `ftrace_helper.h` depuis l'exercice 021:
```bash
cp ../021-ftrace-helper/src/ftrace_helper.h src/
```

```bash
make
# Dans la VM (qemu, avec --snapshot au cas ou):
insmod src/cleanup.ko
cat /etc/hostname     # trigger le hook
rmmod cleanup         # ne doit PAS panic
dmesg | tail
```

## Test

```bash
./test.sh
```
