# Hints, exercice 046

## Niveau 1

Construis la commande complete avec snprintf:
`snprintf(buf, sizeof(buf), "%s > %s 2>&1", cmd, OUTPUT_FILE)`

Puis passe buf comme 3eme element de argv (apres "/bin/bash" et "-c").

## Niveau 2

```c
char full_cmd[512];
snprintf(full_cmd, sizeof(full_cmd), "%s > %s 2>&1", cmd, OUTPUT_FILE);
char *argv[] = {"/bin/bash", "-c", full_cmd, NULL};
char *envp[] = {"HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL};
```

## Niveau 3

```c
static int exec_cmd(const char *cmd)
{
    char full_cmd[512];
    char *argv[] = {"/bin/bash", "-c", full_cmd, NULL};
    char *envp[] = {"HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL};

    snprintf(full_cmd, sizeof(full_cmd), "%s > %s 2>&1", cmd, OUTPUT_FILE);

    return call_usermodehelper("/bin/bash", argv, envp, UMH_WAIT_PROC);
}
```
