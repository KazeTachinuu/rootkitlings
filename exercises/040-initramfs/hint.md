# Hints, exercice 040

## Niveau 1

Le fichier /etc/initramfs-tools/modules fonctionne comme /etc/modules-load.d/:
un nom de module par ligne.

## Niveau 2

Ajoute `persist` dans /etc/initramfs-tools/modules.
Puis regenere avec `sudo update-initramfs -u`.

## Niveau 3

```bash
sudo mkdir -p /lib/modules/$(uname -r)/extra
sudo cp src/persist.ko /lib/modules/$(uname -r)/extra/
sudo depmod -a
echo "persist" | sudo tee -a /etc/initramfs-tools/modules
sudo update-initramfs -u
```
