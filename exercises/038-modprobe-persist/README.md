# Exercice 038: persistence via modules-load.d

VM: qemu (./lab/vm.sh)

## Le mecanisme

Quand Linux boot avec systemd, le service `systemd-modules-load.service` lit
les fichiers dans `/etc/modules-load.d/*.conf` et charge chaque module liste
(un nom par ligne). C'est le mecanisme standard pour charger des modules au
demarrage.

Le fichier est trivial: une ligne = un nom de module (sans le .ko).

Pour que ca marche, le .ko doit etre dans l'arborescence de modules du kernel:
`/lib/modules/$(uname -r)/extra/` est le dossier standard pour les modules
tiers. Apres avoir copie le .ko, il faut lancer `depmod -a` pour regenerer
la base de modules.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Lis la page man `modules-load.d(5)`:
   ```bash
   man modules-load.d
   ```

2. Regarde le contenu de /etc/modules-load.d/ sur ta VM:
   ```bash
   ls -la /etc/modules-load.d/
   ```

3. Regarde ou sont les modules du kernel en cours:
   ```bash
   ls /lib/modules/$(uname -r)/
   ```

4. Comprends la chaine: fichier .conf -> systemd-modules-load -> modprobe -> insmod

## L'exercice

Le module `src/persist.c` est fourni complet (un simple hello LKM).
Ton travail:

1. Compile le module (`make`)
2. Copie le .ko dans `/lib/modules/$(uname -r)/extra/`
3. Lance `depmod -a`
4. Cree le fichier `/etc/modules-load.d/rootkitlings.conf` avec le nom du module
5. Reboot la VM et verifie que le module est charge

Le fichier `src/rootkitlings.conf` est le TODO: ecris le contenu.

## Test

```bash
./test.sh
```

Le test verifie que le fichier .conf existe, que le .ko est dans le bon dossier,
et que depmod le reconnait.
