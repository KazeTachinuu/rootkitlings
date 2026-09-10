# Exercice 039: persistence via systemd service

VM: qemu (./lab/vm.sh)

## Le mecanisme

systemd peut charger un module kernel via un service unit. On ecrit un fichier
`.service` qui appelle `insmod` au demarrage. C'est plus flexible que
modules-load.d car on peut controler l'ordre, les dependances, et les actions
en cas d'echec.

Un service unit minimal:

```ini
[Unit]
Description=...
After=...

[Service]
Type=oneshot
ExecStart=...
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

`Type=oneshot` dit a systemd que le service fait son boulot et se termine
(insmod charge le module et retourne). `RemainAfterExit=yes` dit que le
service reste "active" meme apres la fin de la commande.

Reference: `man systemd.service`, `man systemd.unit`

## Ce qu'il faut comprendre AVANT d'ecrire

1. Regarde un service existant pour voir la structure:
   ```bash
   cat /lib/systemd/system/ssh.service
   ```

2. Comprends les sections [Unit], [Service], [Install]:
   ```bash
   man systemd.service
   ```

3. Comprends `WantedBy=multi-user.target` (equivalent du runlevel 3, le mode normal).

4. Comprends la difference entre `insmod` (chemin absolu obligatoire) et
   `modprobe` (cherche dans /lib/modules/).

## L'exercice

Le module `src/persist.c` est fourni complet (meme module que l'exo 038).
Ton travail: complete le fichier `src/rootkitlings.service` pour qu'il charge
le module au boot via insmod.

Le .ko doit etre copie a un chemin fixe (/opt/rootkitlings/persist.ko par exemple)
car insmod a besoin du chemin absolu.

## Test

```bash
./test.sh
```

Le test verifie la syntaxe du .service, l'installe, et tente un chargement.
