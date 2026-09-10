# Hints, exercice 039

## Niveau 1

Les champs a remplir sont: Description, After, Type, ExecStart, RemainAfterExit, WantedBy.
Le Type est `oneshot` car insmod s'execute et retourne immediatement.

## Niveau 2

```ini
Description=Load rootkitlings kernel module
After=network.target
Type=oneshot
ExecStart=/sbin/insmod /opt/rootkitlings/persist.ko
RemainAfterExit=yes
WantedBy=multi-user.target
```

## Niveau 3

Fichier complet:
```ini
[Unit]
Description=Load rootkitlings kernel module
After=network.target

[Service]
Type=oneshot
ExecStart=/sbin/insmod /opt/rootkitlings/persist.ko
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

Puis:
```bash
sudo mkdir -p /opt/rootkitlings
sudo cp src/persist.ko /opt/rootkitlings/
sudo cp src/rootkitlings.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable rootkitlings
```
