# Deepdive 001

## Que se passe-t-il vraiment quand tu fais insmod

Tu as vu que `insmod` appelle le syscall `init_module`. Mais il se passe
beaucoup de choses entre le syscall et l'appel de ta fonction d'init.

Ouvre `kernel/module/main.c` et lis la fonction `load_module()`. C'est long
(plusieurs centaines de lignes) mais les grandes etapes sont:

1. `module_sig_check` : verification de la signature cryptographique du module
   (si CONFIG_MODULE_SIG est active). C'est comme ca que Secure Boot empeche
   le chargement de modules non signes.

2. `elf_validity_check` : verification que le .ko est un ELF valide

3. `layout_and_allocate` : le kernel alloue de la memoire pour le code et les
   donnees du module. C'est de la memoire kernel (vmalloc).

4. `simplify_symbols` + `apply_relocations` : resolution des symboles. Quand
   ton module appelle `printk`, le .ko contient une reference non resolue.
   Le kernel remplace cette reference par l'adresse reelle de `printk` en
   memoire.

5. `complete_formation` : le module est mis dans la liste globale `modules`.
   C'est a ce moment qu'il apparait dans `lsmod`.

6. Appel de ta fonction `init_module` (l'alias de `hello_init`)

Question: si un attaquant veut charger un rootkit sur un systeme avec Secure
Boot + module signing, quelles sont ses options?

Cherche `CONFIG_MODULE_SIG_FORCE` dans la config kernel. Quelle est la
difference entre `CONFIG_MODULE_SIG` et `CONFIG_MODULE_SIG_FORCE`?


## struct module en memoire

Quand ton module est charge, le kernel cree une `struct module` pour lui.
Cette structure vit en memoire kernel tant que le module est charge.

Le champ `list` (type `struct list_head`) est un noeud dans une liste
doublement chainee de TOUS les modules charges. `lsmod` parcourt cette liste.

Tu le reverras a l'exercice 026 (cacher le module de lsmod). L'idee sera de
retirer ton `struct module` de cette liste sans decharger le module. Le code
continue de tourner, mais `lsmod` ne le voit plus.

Le champ `mkobj` (type `struct module_kobject`) est la representation du
module dans sysfs (`/sys/module/nom`). Tu le reverras a l'exercice 027.


## Tainted kernel

Quand tu charges un module, le kernel verifie sa licence et potentiellement
d'autres choses (signature, etc.). Si quelque chose ne va pas, le kernel
devient "tainted".

```bash
cat /proc/sys/kernel/tainted
```

La valeur est un bitmask. `man 5 proc` liste les bits. Par exemple:
- Bit 0 (1) : module proprietary charge
- Bit 12 (4096) : module non signe charge

Apres avoir charge ton module, verifie si la valeur a change. Si elle est a 0,
ton module GPL n'a rien tainted. Essaie de changer MODULE_LICENSE en
"Proprietary" et regarde ce qui se passe.
