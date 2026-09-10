# Hints, exercice 021

## Niveau 1

hook_kill a la meme signature que les hooks des exercices precedents.
Les arguments du syscall sont dans les registres: di = arg1 (pid), si = arg2 (sig).

## Niveau 2

```c
static asmlinkage long hook_kill(const struct pt_regs *regs)
{
    pid_t pid = regs->di;
    int sig = regs->si;

    printk(KERN_INFO "rootkitlings/021: kill(%d, %d) from %s\n",
           pid, sig, current->comm);
    return orig_kill(regs);
}
```

## Niveau 3

Le tableau hooks:
```c
static struct ftrace_hook hooks[] = {
    HOOK("__x64_sys_kill", hook_kill, &orig_kill),
};
```
C'est tout. ftrace_helper.h fait le reste.
