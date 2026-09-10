# Hints, exercice 020

## Niveau 1

Deux TODOs:
1. Dans hook_openat2: ajouter un printk AVANT l'appel a orig_openat2
2. Dans ftrace_callback: le check within_module + la redirection regs->ip

## Niveau 2

hook_openat2:
```c
printk(KERN_INFO "rootkitlings/020: openat2 hooked\n");
return orig_openat2(regs);
```

La callback:
```c
if (!within_module(parent_ip, THIS_MODULE))
    regs->ip = (unsigned long) hook_openat2;
```

## Niveau 3

Le check within_module est la: sans lui, quand hook_openat2 appelle
orig_openat2 (= do_sys_openat2), ftrace re-trigger la callback,
qui redirige encore vers hook_openat2, et on boucle infiniment.
Avec le check, si l'appel vient de ton module, on ne redirige pas.
