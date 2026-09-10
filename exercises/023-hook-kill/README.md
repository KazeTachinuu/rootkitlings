# Exercice 023: hooker kill, detecter un signal magique

## Le mecanisme

Le syscall kill(pid, sig) envoie un signal a un processus. Le hook de kill
est une technique classique des rootkits pour recevoir des commandes
depuis l'espace utilisateur sans creer de fichier, socket, ou /proc visible.

L'idee: un signal qui n'existe pas normalement (ex: 63 ou 64) est utilise
comme "commande magique". Quand le rootkit detecte ce signal, il execute
une action (donner root, cacher un fichier, etc.) au lieu de transmettre
le signal.

Diamorphine utilise exactement cette technique:
- Signal 31: cacher/montrer un processus (toggle)
- Signal 63: donner root au processus appelant
- Signal 64: cacher/montrer le module (toggle)

L'implementation du syscall kill est dans `kernel/signal.c`:
- `__x64_sys_kill` : le wrapper syscall
- `do_send_sig_info` / `send_signal` : la logique d'envoi

Defini dans:
- `kernel/signal.c`
  https://elixir.bootlin.com/linux/latest/source/kernel/signal.c

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre kernel/signal.c et cherche SYSCALL_DEFINE2(kill, ...).
2. Les arguments de kill(pid, sig) dans pt_regs:
   - regs->di = pid (premier argument)
   - regs->si = sig (deuxieme argument)
3. Si le signal est ton signal magique, tu ne dois PAS appeler l'original.
   Retourne 0 (succes) directement pour que le processus appelant ne
   recoive pas d'erreur.

## L'exercice

Dans `src/hookkill.c`, hooke `__x64_sys_kill`. Quand le signal est 63,
affiche un message et retourne 0 sans transmettre le signal.
Pour tous les autres signaux, appelle l'original normalement.

Copie `ftrace_helper.h` depuis l'exercice 021:
```bash
cp ../021-ftrace-helper/src/ftrace_helper.h src/
```

```bash
make
# Dans la VM (qemu):
insmod src/hookkill.ko
kill -63 1
dmesg | tail
kill -9 99999        # signal normal, doit passer a l'original
rmmod hookkill
```

## Test

```bash
./test.sh
```
