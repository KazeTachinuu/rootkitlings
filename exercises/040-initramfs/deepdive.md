# Deepdive 040: initramfs persistence

## Question ouverte

L'initramfs est une archive cpio. Un defenseur peut l'extraire et inspecter
son contenu:

```bash
mkdir /tmp/initrd && cd /tmp/initrd
unmkinitramfs /boot/initrd.img-$(uname -r) .
find . -name "*.ko" | sort
```

Comment un rootkit avance pourrait-il survivre meme a une reinstallation
de l'initramfs ? Piste: hooks initramfs-tools.

Regarde /etc/initramfs-tools/hooks/ et /usr/share/initramfs-tools/hooks/.
Ces scripts sont executes a chaque `update-initramfs`. Un rootkit pourrait
y placer un script qui re-injecte le .ko a chaque regeneration.

Cherche "initramfs persistence backdoor" pour voir des techniques documentees.

L'exercice 041 explore une approche differente: le module se recharge
lui-meme apres un rmmod, sans fichier de config.
