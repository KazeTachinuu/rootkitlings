# Exercice 020: hook ftrace complet, redirection d'execution

## Le mecanisme

A l'exercice 019, ta callback ftrace faisait juste un printk. Maintenant,
tu vas REDIRIGER l'execution: quand le kernel appelle do_sys_openat2,
ta callback va changer le registre IP pour que l'execution aille vers
TA fonction a la place.

Le principe:
1. Ftrace appelle ta callback quand do_sys_openat2 est sur le point
   de s'executer. Les registres sont sauvegardes dans struct pt_regs.
2. Tu modifies regs->ip pour pointer vers ta fonction de remplacement.
3. Le kernel "retourne" de ftrace et saute vers ta fonction au lieu
   de l'originale.
4. Ta fonction fait ce qu'elle veut, puis appelle l'originale si besoin.

Pour acceder aux registres depuis ftrace_regs:
```c
struct pt_regs *regs = ftrace_get_regs(fregs);
```

Pour rediriger:
```c
regs->ip = (unsigned long) ta_fonction_hook;
```

Le pattern complet d'un hook:
```c
static asmlinkage long (*orig_func)(const struct pt_regs *);

static asmlinkage long hook_func(const struct pt_regs *regs)
{
    // ton code (log, filtre, etc.)
    return orig_func(regs);  // appelle l'originale
}

static void notrace ftrace_callback(unsigned long ip,
                                    unsigned long parent_ip,
                                    struct ftrace_ops *ops,
                                    struct ftrace_regs *fregs)
{
    struct pt_regs *regs = ftrace_get_regs(fregs);
    if (!within_module(parent_ip, THIS_MODULE))
        regs->ip = (unsigned long) hook_func;
}
```

Le check `within_module(parent_ip, THIS_MODULE)` est critique: il evite
que ton hook s'appelle lui-meme en boucle si hook_func appelle orig_func.

Defini dans:
- `include/linux/ftrace.h` : ftrace_get_regs
  https://elixir.bootlin.com/linux/latest/source/include/linux/ftrace.h
- `arch/x86/include/asm/ptrace.h` : struct pt_regs
  https://elixir.bootlin.com/linux/latest/source/arch/x86/include/asm/ptrace.h

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre arch/x86/include/asm/ptrace.h et regarde struct pt_regs.
   Le champ `ip` (instruction pointer) est celui qu'on modifie.

2. Comprends `within_module(addr, mod)` dans include/linux/module.h.
   Elle retourne true si addr est dans le code du module mod.

3. Le pointeur `orig_func` stocke l'adresse de la vraie fonction.
   On l'obtient au init (c'est l'adresse qu'on a resolue via kprobe).

4. `asmlinkage` indique que la fonction recoit ses arguments via la stack
   (ou via pt_regs pour les syscalls). C'est la convention du kernel.

## L'exercice

Dans `src/ftracehook.c`, le setup ftrace est deja fait (exo 019).
Complete la callback pour rediriger vers hook_func, et ecris hook_func
pour logger les appels avant de passer a l'originale.

```bash
make
# Dans la VM (qemu):
insmod src/ftracehook.ko
ls /tmp
dmesg | tail -20
rmmod ftracehook
```

## Test

```bash
./test.sh
```
