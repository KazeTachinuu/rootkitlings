# Hints, exercice 022

## Niveau 1

Le pathname est un pointeur userspace dans regs->si.
Tu ne peux pas le dereferencer directement, utilise strncpy_from_user.

## Niveau 2

```c
copied = strncpy_from_user(dirname, (const char __user *)regs->si, sizeof(dirname));
if (copied > 0)
    printk(KERN_INFO "rootkitlings/022: mkdir %s by %s (pid %d)\n",
           dirname, current->comm, current->pid);
```

## Niveau 3

Code complet du TODO:
```c
copied = strncpy_from_user(dirname, (const char __user *)regs->si, sizeof(dirname));
if (copied > 0)
    printk(KERN_INFO "rootkitlings/022: mkdir %s by %s (pid %d)\n",
           dirname, current->comm, current->pid);
return orig_mkdirat(regs);
```
Remplace la ligne `return orig_mkdirat(regs);` existante.
