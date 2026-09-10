# Hints, exercice 019

## Niveau 1

La callback ftrace doit etre marquee `notrace` pour eviter la recursion.
Le premier TODO est la callback, le deuxieme est l'assignation dans la struct,
le troisieme est le setup dans init.

## Niveau 2

```c
static void notrace ftrace_callback(unsigned long ip,
                                    unsigned long parent_ip,
                                    struct ftrace_ops *ops,
                                    struct ftrace_regs *fregs)
{
    printk(KERN_INFO "rootkitlings/019: hooked function called\n");
}
```
Et: `.func = ftrace_callback,`

## Niveau 3

Dans l'init:
```c
ret = ftrace_set_filter_ip(&ftrace_ops, target_func_addr, 0, 0);
if (ret < 0) {
    printk(KERN_ERR "rootkitlings/019: ftrace_set_filter_ip failed: %d\n", ret);
    return ret;
}
ret = register_ftrace_function(&ftrace_ops);
if (ret < 0) {
    printk(KERN_ERR "rootkitlings/019: register_ftrace_function failed: %d\n", ret);
    return ret;
}
```
