# Hints: 010 kthread

## Niveau 1

La fonction du thread doit boucler tant que kthread_should_stop()
retourne false. N'oublie pas de dormir dans la boucle, sinon tu
monopolises le CPU.

## Niveau 2

```c
static int thread_fn(void *data)
{
    while (!kthread_should_stop()) {
        counter++;
        printk(KERN_INFO "rootkitlings/010: tick %d\n", counter);
        ssleep(1);
    }
    return 0;
}
```

## Niveau 3

```c
static int thread_fn(void *data)
{
    while (!kthread_should_stop()) {
        counter++;
        printk(KERN_INFO "rootkitlings/010: tick %d\n", counter);
        ssleep(1);
    }
    return 0;
}

// init:
thread = kthread_run(thread_fn, NULL, "rootkitlings");
if (IS_ERR(thread))
    return PTR_ERR(thread);

// exit:
kthread_stop(thread);
```
