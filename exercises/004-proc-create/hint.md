# Hints: 004 proc-create

## Niveau 1

`struct proc_ops` est une structure avec des pointeurs de fonctions.
Tu dois la remplir avec les callbacks pour open, read, lseek, release.

## Niveau 2

```c
static const struct proc_ops rk_proc_ops = {
    .proc_open    = rk_open,
    .proc_read    = seq_read,
    // ajoute lseek et release
};
```

## Niveau 3

```c
static const struct proc_ops rk_proc_ops = {
    .proc_open    = rk_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

// dans init:
proc_entry = proc_create(PROC_NAME, 0444, NULL, &rk_proc_ops);
if (!proc_entry)
    return -ENOMEM;

// dans exit:
proc_remove(proc_entry);
```
