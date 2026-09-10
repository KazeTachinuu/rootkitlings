# Lab, dev kernel pour EpiRootkit

Deux modes selon le TP.

| Mode | Script | Boot | Isolation | Quand l'utiliser |
|------|--------|------|-----------|------------------|
| Fast | ./vng.sh | 3s | FS hote read-only | Rootkit 101: hello LKM, printk, /proc, ftrace de base |
| Safe | ./vm.sh | 20s | totale (Debian jetable) | hooks reels, hide files, persistence, network |

Regle: des que ton module fait autre chose que du printk, passe en mode Safe.

## Setup initial (une fois)

```bash
cd lab
./setup.sh
./build-vm.sh
```

Resultat: build/disk.qcow2 (image Debian) et build/cloud-init.iso.

## Workflow d'un TP

Rootkit 101 (hello LKM, exploration):

```bash
cd tps/tp1
make
../../lab/vng.sh --rwdir .
# dans la VM
insmod hello.ko
dmesg | tail
rmmod hello
```

TP avance (hook, hide, etc):

```bash
cd tps/tp3-hide
make
../../lab/vm.sh --share . --snapshot
# ssh depuis un autre terminal
ssh -p 2222 root@localhost   # password: root
cp /mnt/host/hide.ko /root/
insmod /root/hide.ko
poweroff                     # rollback auto grace a --snapshot
```

## Debug GDB

En mode Safe:

```bash
./vm.sh --share . --gdb
```

Dans un autre terminal:

```bash
gdb build/vmlinux
(gdb) target remote :1234
(gdb) break sys_openat
(gdb) continue
```

## Kernel configs

Voir configs/ pour la progression pedagogique:

- default.config: protections OFF (KASLR, SMEP, module signing), premiers TPs
- hardened.config: tout ON, TP contournement

## Alternative Vagrant

Si Vagrant est prefere (evoque en discussion):

```bash
# lab/Vagrantfile a creer, non fourni par defaut
vagrant up
vagrant ssh
```

Vagrant est plus lourd (VirtualBox 2GB) que QEMU natif, mais plus familier aux etudiants qui viennent du dev web.

## Problemes courants

"KVM: disabled by BIOS": active VT-x/AMD-V dans le BIOS.

"could not access /dev/kvm": sudo usermod -aG kvm $USER, puis relogin.

"vng: command not found": installe virtme-ng.

```bash
yay -S virtme-ng
# ou
pipx install virtme-ng
```

"insmod: Invalid module format": headers et kernel guest ne matchent pas. En mode Fast le guest est le kernel de l'hote, donc pacman -Syu puis reboot. En mode Safe le module doit etre compile contre les headers du guest Debian (ou tu compiles dans la VM directement).

"kernel panic dans la VM": normal en dev rootkit. En mode Fast, Ctrl+D ne marche plus, kill QEMU avec Ctrl+A puis X. En mode Safe avec --snapshot la VM est propre au prochain boot.
