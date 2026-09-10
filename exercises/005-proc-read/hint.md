# Hints: 005 proc-read

## Niveau 1

`current` est une macro qui retourne un pointeur vers le `struct task_struct`
du processus en cours d'execution. `current->comm` est son nom, `current->pid`
son PID.

## Niveau 2

```c
static int rk_show(struct seq_file *m, void *v)
{
    seq_printf(m, "rootkitlings/005: reader=%s pid=...\n",
               current->comm, ...);
    return 0;
}
```

## Niveau 3

```c
static int rk_show(struct seq_file *m, void *v)
{
    seq_printf(m, "rootkitlings/005: reader=%s pid=%d\n",
               current->comm, current->pid);
    return 0;
}
```
