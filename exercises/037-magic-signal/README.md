# Exercice 037: magic signal for privilege escalation

VM: qemu (./lab/vm.sh)

## Le mecanisme

Les rootkits Diamorphine et Reptile utilisent un "signal magic" comme
trigger. L'idee: hooker le syscall kill(), detecter un numero de signal
special (par exemple 64), et au lieu de tuer le processus, donner root
au processus qui envoie le signal.

Signal 64 est le dernier signal "temps reel" Linux (SIGRTMAX). Aucun
programme normal ne l'envoie. C'est un canal furtif.

Le syscall kill est `__x64_sys_kill` (sur x86_64). Quand on le hooke:

```c
int sig = regs->si;  // deuxieme argument de kill(pid, sig)
int pid = regs->di;  // premier argument
```

Si `sig == 64`, on ne transmet pas le signal. A la place, on fait
prepare_kernel_cred(NULL) + commit_creds sur le processus appelant
(current). Le processus qui a fait `kill -64` devient root.

C'est la combinaison de Phase 3 (hooking) et Phase 5 (privesc).

## Ce qu'il faut comprendre AVANT d'ecrire

1. Lis `__x64_sys_kill` dans `kernel/signal.c`:
   https://elixir.bootlin.com/linux/latest/source/kernel/signal.c

2. Les arguments du syscall kill(pid, sig) arrivent dans:
   - regs->di = pid (premier argument)
   - regs->si = sig (deuxieme argument)
   (convention x86_64 syscall)

3. ftrace_helper.h (fourni) gere le hook.

4. Quand on intercepte le signal magic, on retourne 0 (succes) sans
   appeler l'original. Le signal n'est jamais delivre.

## L'exercice

Dans `src/magicsig.c`, le hook de kill est en place. A toi de:
1. Extraire le signal depuis regs->si
2. Si c'est MAGIC_SIG (64), faire l'escalation et retourner 0
3. Sinon, appeler l'original

8 lignes de code.

Compile avec `make`, puis lance `test.sh` dans la VM.

## Test

```bash
./test.sh
```
