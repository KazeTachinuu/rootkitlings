# Exercice 013: struct task_struct et current

## Le mecanisme

Chaque processus (et chaque thread) dans Linux est represente en kernel par
une `struct task_struct`. C'est LA structure centrale du scheduler. Elle
contient le PID, le nom du processus (comm), les credentials, l'etat,
les file descriptors, la memoire virtuelle, et des centaines d'autres champs.

La macro `current` retourne un pointeur vers la task_struct du processus
en cours d'execution sur le CPU courant. Sur x86_64, `current` lit un
registre per-CPU (gs segment) pour trouver la structure sans parcourir de liste.

Defini dans:
- `include/linux/sched.h` : struct task_struct
  https://elixir.bootlin.com/linux/latest/source/include/linux/sched.h
- `arch/x86/include/asm/current.h` : macro current sur x86
  https://elixir.bootlin.com/linux/latest/source/arch/x86/include/asm/current.h

Les champs qui nous interessent pour un rootkit:
```c
struct task_struct {
    pid_t pid;                    // PID du thread
    pid_t tgid;                   // PID du processus (thread group ID)
    char comm[TASK_COMM_LEN];     // nom du processus (16 chars max)
    const struct cred *cred;      // credentials actuelles
    struct mm_struct *mm;         // espace memoire (NULL pour les kthreads)
    // ... des centaines d'autres champs
};
```

Quand tu fais `insmod`, c'est le processus insmod qui execute module_init.
Donc `current` dans module_init pointe vers la task_struct de insmod.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre struct task_struct sur Elixir Bootlin:
   https://elixir.bootlin.com/linux/latest/source/include/linux/sched.h
   Cherche `struct task_struct {`. Note la taille de la structure (cherche
   le `}` de fermeture, c'est a plusieurs centaines de lignes).

2. Regarde la macro current pour x86_64:
   https://elixir.bootlin.com/linux/latest/source/arch/x86/include/asm/current.h
   Comprends que c'est un acces per-CPU sans lock.

3. Note la difference entre `pid` et `tgid`. Pour un processus single-thread,
   les deux sont identiques. Pour un thread, `pid` est le TID et `tgid` est
   le PID du processus parent du thread group.

## L'exercice

Dans `src/taskinfo.c`, complete module_init pour afficher les informations
du processus courant: PID, nom (comm), et TGID.

```bash
make
# Dans la VM:
insmod src/taskinfo.ko
dmesg | tail
rmmod taskinfo
```

Tu devrais voir le PID et le nom "insmod" dans dmesg.

## Test

```bash
./test.sh
```
