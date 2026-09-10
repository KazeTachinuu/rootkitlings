# Exercice 041: self-reload via call_usermodehelper

VM: qemu (./lab/vm.sh)

## Le mecanisme

`call_usermodehelper` permet au kernel d'executer un programme userspace.
C'est la meme API utilisee par le kernel pour lancer /sbin/modprobe quand
un module est demande.

Prototype dans `include/linux/umh.h`:
```c
int call_usermodehelper(const char *path, char **argv, char **envp, int wait);
```

Le parametre `wait` peut etre:
- `UMH_WAIT_EXEC`: le kernel attend que le fork+exec reussisse
- `UMH_WAIT_PROC`: le kernel attend que le process termine
- `UMH_NO_WAIT`: le kernel n'attend pas (fire and forget)

L'idee: dans module_exit, on lance un programme userspace qui recharge le
module. Quand un admin fait `rmmod`, le module se decharge proprement MAIS
un script en userspace le recharge immediatement.

Source kernel:
- https://elixir.bootlin.com/linux/latest/source/include/linux/umh.h
- https://elixir.bootlin.com/linux/latest/source/kernel/umh.c

## Ce qu'il faut comprendre AVANT d'ecrire

1. Lis le prototype de call_usermodehelper dans include/linux/umh.h
2. Regarde comment le kernel l'utilise deja:
   ```bash
   grep -rn "call_usermodehelper" kernel/ --include="*.c" | head -10
   ```
3. Comprends pourquoi on utilise UMH_NO_WAIT dans module_exit:
   on ne peut pas attendre la fin d'un process dans module_exit
   car insmod prendrait un verrou deja tenu.

## L'exercice

Dans `src/reload.c`, complete la fonction `reload_exit` pour appeler
call_usermodehelper. Le module doit executer un script shell qui attend
1 seconde puis insmod le module.

Le script `src/reload.sh` est fourni.
Copie-le dans /tmp/reload.sh avant de charger le module.

## Test

```bash
./test.sh
```

Le test charge le module, le decharge, attend 3 secondes, et verifie qu'il
est revenu dans lsmod.
