# Hints, exercice 023

## Niveau 1

C'est un simple if/else: si le signal est 63, log et return 0.
Sinon, passe a l'original.

## Niveau 2

```c
if (sig == MAGIC_SIGNAL) {
    printk(KERN_INFO "rootkitlings/023: magic signal %d received for pid %d from %s\n",
           sig, pid, current->comm);
    return 0;
}
return orig_kill(regs);
```

## Niveau 3

Remplace le return a la fin du TODO par le code du niveau 2.
Le return 0 est important: il dit au processus appelant que kill() a "reussi"
sans que le signal soit reellement envoye.
