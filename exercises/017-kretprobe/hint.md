# Hints, exercice 017

## Niveau 1

Le ret_handler est similaire au pre_handler de l'exercice 016.
La difference: la valeur de retour est dans regs->ax (registre RAX).

## Niveau 2

```c
static int ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    int retval = regs->ax;
    printk(KERN_INFO "rootkitlings/017: do_mkdirat returned %d\n", retval);
    return 0;
}
```

## Niveau 3

Dans la struct: `.handler = ret_handler,`
Dans l'init:
```c
ret = register_kretprobe(&kretp);
if (ret < 0) {
    printk(KERN_ERR "rootkitlings/017: register_kretprobe failed: %d\n", ret);
    return ret;
}
```
