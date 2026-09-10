# Exercice 021: ftrace_helper.h, hooking simplifie

## Le mecanisme

Les exercices 019 et 020 t'ont montre le mecanisme complet du hooking ftrace:
callback, anti-recursion, filter, register. C'est beaucoup de boilerplate
pour chaque hook.

TheXcellerator (xcellerator.github.io) a cree un header `ftrace_helper.h`
qui encapsule tout ca dans des macros. C'est devenu le pattern standard
utilise par la plupart des rootkits open-source (Diamorphine, Reptile, etc.).

Le header fournit:
- `struct ftrace_hook` : structure qui contient le nom, le pointeur original, et le ftrace_ops
- `fh_install_hook` / `fh_remove_hook` : installer/retirer un hook en une ligne
- `fh_install_hooks` / `fh_remove_hooks` : installer/retirer plusieurs hooks d'un coup

Usage simplifie:
```c
static asmlinkage long (*orig_kill)(const struct pt_regs *);

static asmlinkage long hook_kill(const struct pt_regs *regs)
{
    // ton code
    return orig_kill(regs);
}

static struct ftrace_hook hooks[] = {
    HOOK("__x64_sys_kill", hook_kill, &orig_kill),
};
```

Reference: xcellerator.github.io/posts/linux_rootkits_01/

## Ce qu'il faut comprendre AVANT d'ecrire

1. Lis `src/ftrace_helper.h` fourni dans cet exercice. Retrouve les
   concepts de l'exercice 019-020: ftrace_ops, set_filter_ip,
   register_ftrace_function, le callback avec within_module.

2. La macro HOOK genere une struct ftrace_hook avec le nom de la fonction,
   le pointeur vers ton hook, et l'adresse ou stocker l'original.

3. `fh_install_hooks` appelle le trick kprobe pour resoudre les noms,
   configure les filtres, et enregistre les callbacks.

## L'exercice

Dans `src/easyhook.c`, utilise ftrace_helper.h pour hooker `__x64_sys_kill`.
Le hook doit logger chaque appel a kill() avec le PID cible et le signal.

Le header `ftrace_helper.h` est deja fourni dans `src/`.

```bash
make
# Dans la VM (qemu):
insmod src/easyhook.ko
kill -0 1        # envoie signal 0 au PID 1 (test si le process existe)
dmesg | tail
rmmod easyhook
```

## Test

```bash
./test.sh
```
