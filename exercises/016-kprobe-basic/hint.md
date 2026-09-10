# Hints, exercice 016

## Niveau 1

Le pre_handler est une simple fonction C qui fait un printk et retourne 0.
Le champ .pre_handler de la struct kprobe pointe vers cette fonction.

## Niveau 2

```c
static int handler_pre(struct kprobe *p, struct pt_regs *regs)
{
    printk(KERN_INFO "rootkitlings/016: %s called\n", p->symbol_name);
    return 0;
}
```
Et dans la struct: `.pre_handler = handler_pre,`

## Niveau 3

Code complet de l'init:
```c
ret = register_kprobe(&kp);
if (ret < 0) {
    printk(KERN_ERR "rootkitlings/016: register_kprobe failed: %d\n", ret);
    return ret;
}
```
