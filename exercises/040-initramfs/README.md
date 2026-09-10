# Exercice 040: persistence via initramfs

VM: qemu (./lab/vm.sh)

## Le mecanisme

L'initramfs (initial RAM filesystem) est une archive compressees chargee en
memoire par le bootloader AVANT le vrai systeme de fichiers. Le kernel la
decompresse, monte son contenu comme / temporaire, et execute /init.

Sur Debian, initramfs est gere par initramfs-tools. Pour inclure un module
kernel dans l'initramfs:
1. Le .ko doit etre dans /lib/modules/$(uname -r)/
2. Le nom du module doit etre liste dans /etc/initramfs-tools/modules
3. On regenere avec `update-initramfs -u`

C'est la persistence la plus profonde sans modifier le bootloader: le module
est charge AVANT le systeme de fichiers root, avant systemd, avant tout.

Reference:
- `man initramfs-tools`
- `man update-initramfs`
- `/etc/initramfs-tools/modules`

## Ce qu'il faut comprendre AVANT d'ecrire

1. Regarde le contenu de l'initramfs actuel:
   ```bash
   lsinitramfs /boot/initrd.img-$(uname -r) | head -30
   ```

2. Regarde quels modules sont deja inclus:
   ```bash
   lsinitramfs /boot/initrd.img-$(uname -r) | grep "\.ko"
   ```

3. Lis le fichier de config:
   ```bash
   cat /etc/initramfs-tools/modules
   ```

4. Comprends que l'initramfs est une archive cpio compressee:
   ```bash
   file /boot/initrd.img-$(uname -r)
   ```

## L'exercice

Le module `src/persist.c` est fourni complet.
Ton travail:

1. Compile le module
2. Copie le .ko dans `/lib/modules/$(uname -r)/extra/`
3. Lance `depmod -a`
4. Ajoute le nom du module dans `/etc/initramfs-tools/modules`
5. Regenere l'initramfs: `update-initramfs -u`
6. Reboot et verifie que le module est charge

Le fichier `src/initramfs-modules.txt` est le TODO: ecris la ligne a ajouter.

## Test

```bash
./test.sh
```

Le test verifie que le module est dans l'initramfs regenere.
