# Exercice 017: kretprobes, intercepter le retour

## Le mecanisme

Un kretprobe intercepte le RETOUR d'une fonction kernel. Il fonctionne en
deux temps:
1. A l'ENTREE, le entry_handler est appele. Kprobes sauvegarde l'adresse
   de retour originale et la remplace par un trampoline.
2. Au RETOUR, le CPU saute dans le trampoline qui appelle ton ret_handler.
   Tu peux lire (et modifier) la valeur de retour dans regs->ax.

La struct kretprobe:
```c
struct kretprobe {
    struct kprobe kp;              // le kprobe sous-jacent
    kretprobe_handler_t handler;   // appele au RETOUR
    kretprobe_handler_t entry_handler; // appele a l'ENTREE (optionnel)
    int maxactive;                 // nombre max d'instances simultanées
    int data_size;                 // taille des donnees privees par instance
};
```

Chaque instance active (une entree sans retour correspondant) occupe un slot.
Si maxactive est atteint, le prochain appel est ignore. Pour un syscall
tres appele, mets maxactive assez haut (20+).

Defini dans:
- `include/linux/kprobes.h` : struct kretprobe
  https://elixir.bootlin.com/linux/latest/source/include/linux/kprobes.h
- `kernel/kprobes.c` : register_kretprobe
  https://elixir.bootlin.com/linux/latest/source/kernel/kprobes.c

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre la definition de kretprobe_handler_t dans include/linux/kprobes.h.
   La signature est:
   ```c
   typedef int (*kretprobe_handler_t)(struct kretprobe_instance *, struct pt_regs *);
   ```

2. Dans le ret_handler, la valeur de retour de la fonction hookee est dans
   `regs->ax` (RAX sur x86_64). C'est la convention C.

3. L'entry_handler est optionnel. On s'en sert quand on veut capturer les
   arguments a l'entree et les comparer avec le retour.

4. register_kretprobe/unregister_kretprobe dans kernel/kprobes.c.

## L'exercice

Dans `src/kretprobe.c`, complete le ret_handler pour afficher la valeur
de retour de `do_mkdirat` (la fonction interne de mkdir).

```bash
make
# Dans la VM (qemu):
insmod src/kretprobe.ko
mkdir /tmp/rootkitlings_017_test
dmesg | tail
rmmod kretprobe
rmdir /tmp/rootkitlings_017_test
```

Tu devrais voir la valeur de retour (0 = succes, -EEXIST si le dir existe).

## Test

```bash
./test.sh
```
