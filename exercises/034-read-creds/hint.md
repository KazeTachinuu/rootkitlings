# Hints, exercice 034

## Niveau 1

`current_cred()` te donne un pointeur const vers les credentials.
Les champs uid, euid, etc. sont de type `kuid_t`, il faut `.val` pour
obtenir le nombre.

## Niveau 2

```c
const struct cred *cred = current_cred();
pr_info("uid=%u euid=%u", cred->uid.val, cred->euid.val);
```

## Niveau 3

```c
const struct cred *cred = current_cred();
pr_info("rootkitlings/034: uid=%u euid=%u gid=%u egid=%u\n",
        cred->uid.val, cred->euid.val,
        cred->gid.val, cred->egid.val);
```
