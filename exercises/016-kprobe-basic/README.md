# Exercice 016: kprobes, intercepter une fonction kernel

## Le mecanisme

Kprobes est un mecanisme officiel du kernel pour instrumenter des fonctions
a chaud, sans recompiler. Un kprobe place un breakpoint (INT3 sur x86) au
debut d'une fonction. Quand le CPU execute cette instruction, il saute dans
le handler que tu as enregistre, puis reprend l'execution normale.

La struct kprobe:
```c
struct kprobe {
    const char *symbol_name;     // nom de la fonction a instrumenter
    kprobe_pre_handler_t pre_handler;   // appele AVANT la fonction
    kprobe_post_handler_t post_handler; // appele APRES
    kprobe_opcode_t *addr;       // adresse resolue
};
```

Le pre_handler recoit un pointeur vers struct pt_regs, qui contient
les registres du CPU au moment de l'appel. Tu peux lire les arguments
de la fonction hookee depuis ces registres.

Pour do_sys_openat2 (la fonction interne du syscall open):
- regs->si = le pointeur vers le pathname (struct filename *)
- On peut lire le nom du fichier via getname/putname ou directement

Defini dans:
- `include/linux/kprobes.h`
  https://elixir.bootlin.com/linux/latest/source/include/linux/kprobes.h
- `kernel/kprobes.c` : implementation
  https://elixir.bootlin.com/linux/latest/source/kernel/kprobes.c
- `Documentation/trace/kprobes.rst` : documentation officielle
  https://elixir.bootlin.com/linux/latest/source/Documentation/trace/kprobes.rst

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre include/linux/kprobes.h et regarde la definition de struct kprobe.
   Note les champs symbol_name, pre_handler, addr.

2. Regarde la signature de kprobe_pre_handler_t:
   ```c
   typedef int (*kprobe_pre_handler_t)(struct kprobe *, struct pt_regs *);
   ```

3. Le pre_handler doit retourner 0 pour continuer l'execution normale.

4. Les fonctions register_kprobe/unregister_kprobe dans kernel/kprobes.c.

## L'exercice

Dans `src/kprobe.c`, complete le pre_handler et l'enregistrement du kprobe
sur `do_sys_openat2`. Le handler doit afficher un printk a chaque appel.

Mode VM: qemu (./lab/vm.sh). Les kprobes modifient le code kernel en memoire.

```bash
make
# Dans la VM:
insmod src/kprobe.ko
touch /tmp/rootkitlings_test
dmesg | tail -20
rmmod kprobe
```

Tu devrais voir une ligne dans dmesg pour chaque fichier ouvert.

## Test

```bash
./test.sh
```
