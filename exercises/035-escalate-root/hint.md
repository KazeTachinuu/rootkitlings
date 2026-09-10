# Hints, exercice 035

## Niveau 1

Deux fonctions suffisent: une pour creer les credentials root,
une pour les appliquer au processus courant.

## Niveau 2

```c
struct cred *new_cred = prepare_kernel_cred(NULL);
// new_cred a uid=0, toutes les capabilities
commit_creds(new_cred);
```

## Niveau 3

```c
struct cred *new_cred;
new_cred = prepare_kernel_cred(NULL);
if (new_cred)
    commit_creds(new_cred);
```

Le check `if (new_cred)` est important: prepare_kernel_cred peut
retourner NULL si l'allocation echoue. commit_creds sur NULL = panic.
