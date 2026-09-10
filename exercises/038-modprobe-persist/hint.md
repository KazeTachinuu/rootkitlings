# Hints, exercice 038

## Niveau 1

Le fichier .conf contient juste le nom du module, sans extension.
Pas de chemin, pas de .ko, juste le nom.

## Niveau 2

Le nom du module est `persist` (le nom du .c sans le .c).
Le fichier .conf doit contenir une seule ligne: `persist`

## Niveau 3

```
# /etc/modules-load.d/rootkitlings.conf
persist
```

Et les commandes:
```bash
sudo mkdir -p /lib/modules/$(uname -r)/extra
sudo cp src/persist.ko /lib/modules/$(uname -r)/extra/
sudo depmod -a
sudo cp src/rootkitlings.conf /etc/modules-load.d/
```
