# Exercice 019: ftrace_ops, enregistrer une callback ftrace

## Le mecanisme

L'API ftrace permet a un module kernel d'enregistrer une callback qui sera
appelee chaque fois qu'une fonction specifique est executee. C'est plus
propre et plus rapide que les kprobes.

Les etapes:
1. Declarer une `struct ftrace_ops` avec ta callback
2. Utiliser `ftrace_set_filter_ip` pour limiter le hook a une seule fonction
3. Appeler `register_ftrace_function` pour activer le hook
4. A la sortie: `unregister_ftrace_function` puis reset du filtre

La struct ftrace_ops:
```c
struct ftrace_ops {
    ftrace_func_t func;     // ta callback
    unsigned long flags;    // FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_RECURSION | FTRACE_OPS_FL_IPMODIFY
    // ... champs internes
};
```

Les flags importants:
- `FTRACE_OPS_FL_SAVE_REGS` : sauvegarde les registres dans pt_regs (necessaire pour modifier les args)
- `FTRACE_OPS_FL_RECURSION` : autorise la recursion (si le hook appelle une fonction hookee)
- `FTRACE_OPS_FL_IPMODIFY` : autorise la modification de l'IP (necessaire pour rediriger l'execution)

La callback ftrace:
```c
void callback(unsigned long ip, unsigned long parent_ip,
              struct ftrace_ops *op, struct ftrace_regs *fregs);
```
- `ip` : adresse de la fonction hookee
- `parent_ip` : adresse de l'appelant
- `fregs` : les registres (accedes via ftrace_get_regs)

Defini dans:
- `include/linux/ftrace.h`
  https://elixir.bootlin.com/linux/latest/source/include/linux/ftrace.h
- `kernel/trace/ftrace.c` : implementation
  https://elixir.bootlin.com/linux/latest/source/kernel/trace/ftrace.c

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre include/linux/ftrace.h et cherche `struct ftrace_ops`.
   Note le champ `func` et les flags.

2. Cherche `ftrace_set_filter_ip` dans le meme fichier. Elle prend
   l'adresse de la fonction a filtrer.

3. Il te faut l'adresse de la fonction cible. Tu peux l'obtenir avec
   le trick kprobe de l'exercice 012, ou utiliser kallsyms_lookup_name
   si disponible.

4. L'ordre est important: set_filter AVANT register_ftrace_function.

## L'exercice

Dans `src/ftraceops.c`, complete le setup de la struct ftrace_ops et
l'enregistrement. La callback fait un simple printk pour l'instant.

On hooke `do_sys_openat2` (meme cible que l'exercice 016, pour comparer).

```bash
make
# Dans la VM (qemu):
insmod src/ftraceops.ko
cat /etc/hostname
dmesg | tail -20
rmmod ftraceops
```

## Test

```bash
./test.sh
```
