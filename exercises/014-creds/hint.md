# Hints, exercice 014

## Niveau 1

`current_cred()` retourne `const struct cred *`. Tu ne peux que lire,
pas modifier (on fera ca plus tard).
kuid_t n'est pas un int, utilise `__kuid_val()` pour le convertir.

## Niveau 2

```c
cred = current_cred();
printk(KERN_INFO "rootkitlings/014: uid = %u\n", __kuid_val(cred->uid));
```

## Niveau 3

```c
cred = current_cred();
printk(KERN_INFO "rootkitlings/014: uid = %u\n", __kuid_val(cred->uid));
printk(KERN_INFO "rootkitlings/014: gid = %u\n", __kgid_val(cred->gid));
printk(KERN_INFO "rootkitlings/014: euid = %u\n", __kuid_val(cred->euid));
```
