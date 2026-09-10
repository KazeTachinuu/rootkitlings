# Exercice 027: hide from /sys/module

VM: qemu (./lab/vm.sh)

## Le mecanisme

L'exercice 026 cache le module de `lsmod` (qui lit `/proc/modules`). Mais
`/sys/module/` expose aussi chaque module charge via sysfs.

sysfs est un filesystem virtuel qui expose les objets kernel (kobjects) en
arborescence. Chaque module possede un `struct module_kobject` qui le represente
dans `/sys/module/<nom>/`.

Le champ dans `include/linux/module.h`:

```c
struct module {
    // ...
    struct module_kobject mkobj;
    // ...
};
```

Et `struct module_kobject` contient un `struct kobject kobj`.

Pour retirer le module de sysfs, on appelle `kobject_del` sur ce kobject.
La fonction est dans `lib/kobject.c`.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre `include/linux/kobject.h` sur Elixir Bootlin:
   https://elixir.bootlin.com/linux/latest/source/include/linux/kobject.h

2. Regarde la signature de `kobject_del`:
   ```c
   void kobject_del(struct kobject *kobj);
   ```

3. Ouvre `lib/kobject.c` pour voir ce que fait kobject_del:
   https://elixir.bootlin.com/linux/latest/source/lib/kobject.c
   Il retire l'entree de sysfs et deconnecte le parent.

4. Le chemin pour atteindre le kobject du module:
   `THIS_MODULE->mkobj.kobj`

## L'exercice

Dans `src/hidesys.c`, complete `hidesys_init` pour retirer le module de
sysfs. C'est 1 ligne. Combine avec l'exercice 026 (list_del) pour
disparaitre completement.

Compile avec `make`, puis lance `test.sh` dans la VM.

## Test

```bash
./test.sh
```
