# Hints, exercice 013

## Niveau 1

`current` est une macro qui retourne `struct task_struct *`.
Tu peux acceder aux champs directement: `current->pid`, `current->comm`.

## Niveau 2

```c
printk(KERN_INFO "rootkitlings/013: pid = %d\n", current->pid);
```
Le format pour une string (comm) est `%s`, pour un int (pid, tgid) c'est `%d`.

## Niveau 3

```c
printk(KERN_INFO "rootkitlings/013: pid = %d\n", current->pid);
printk(KERN_INFO "rootkitlings/013: comm = %s\n", current->comm);
printk(KERN_INFO "rootkitlings/013: tgid = %d\n", current->tgid);
```
