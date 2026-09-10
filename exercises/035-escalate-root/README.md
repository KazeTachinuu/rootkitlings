# Exercice 035: privilege escalation to root

VM: qemu (./lab/vm.sh)

## Le mecanisme

Le kernel fournit deux fonctions pour manipuler les credentials:

```c
struct cred *prepare_kernel_cred(struct task_struct *daemon);
int commit_creds(struct cred *new);
```

`prepare_kernel_cred(NULL)` cree un nouveau jeu de credentials avec
uid=0, gid=0, et toutes les capabilities. C'est le jeu de credentials
du kernel lui-meme (init_cred).

`commit_creds(cred)` remplace les credentials du processus courant
par les nouvelles. Apres cet appel, le processus est root.

Ces fonctions sont dans `kernel/cred.c`:
https://elixir.bootlin.com/linux/latest/source/kernel/cred.c

L'idee: on cree une entree /proc. Quand un processus ecrit "escalate"
dans cette entree, le module appelle prepare_kernel_cred + commit_creds
dans le contexte de CE processus. Le processus devient root.

C'est la technique d'escalation la plus directe. Dans un vrai rootkit,
le trigger serait un signal magic (exercice 037) ou un paquet reseau.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Lis `prepare_kernel_cred` dans `kernel/cred.c`:
   https://elixir.bootlin.com/linux/latest/source/kernel/cred.c

2. Quand on passe NULL, elle utilise `init_cred` (les credentials du
   processus init, PID 1). Ca donne uid=0 et toutes les capabilities.

3. Lis `commit_creds` dans le meme fichier. Elle remplace les credentials
   du processus courant (current) par les nouvelles.

4. Le write handler d'un /proc entry est execute dans le contexte du
   processus qui ecrit. Donc `current` dans le handler est le processus
   qui a ecrit.

## L'exercice

Dans `src/privesc.c`, la structure /proc est en place. Le write handler
verifie que l'input est "escalate\n". A toi d'ecrire les 2-3 lignes qui
font l'escalation.

Compile avec `make`, puis lance `test.sh` dans la VM.

## Test

```bash
./test.sh
```
