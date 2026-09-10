# Hints, exercice 012

## Niveau 1

Le pattern est: register, lire .addr, unregister. Trois operations.
N'oublie pas de verifier le code de retour de register_kprobe.

## Niveau 2

```c
ret = register_kprobe(&kp);
if (ret < 0) {
    printk(KERN_ERR "register_kprobe failed: %d\n", ret);
    return ret;
}
my_kallsyms_lookup_name = (kallsyms_lookup_name_t) kp.addr;
unregister_kprobe(&kp);
```

## Niveau 3

Le code complet du TODO:
```c
ret = register_kprobe(&kp);
if (ret < 0) {
    printk(KERN_ERR "rootkitlings/012: kprobe failed: %d\n", ret);
    return ret;
}
my_kallsyms_lookup_name = (kallsyms_lookup_name_t) kp.addr;
unregister_kprobe(&kp);

addr = my_kallsyms_lookup_name("commit_creds");
```
Supprime la ligne `addr = 0;` et remplace-la.
