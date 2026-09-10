# Exercice 036: give root to a specific PID

VM: qemu (./lab/vm.sh)

## Le mecanisme

L'exercice 035 donne root au processus qui ecrit dans /proc. Mais dans
un vrai rootkit, on veut donner root a un processus distant (par exemple
un shell obtenu via la connexion C2).

Le kernel peut trouver un processus par son PID avec `find_task_by_vpid`:

```c
struct task_struct *task = find_task_by_vpid(pid);
```

Definie dans `kernel/pid.c`:
https://elixir.bootlin.com/linux/latest/source/kernel/pid.c

Une fois qu'on a le task_struct, on peut modifier ses credentials.
Mais attention: on ne peut pas appeler commit_creds() pour un AUTRE
processus (commit_creds agit sur `current`). Il faut modifier les
credentials directement:

```c
struct cred *new_cred = prepare_creds();
// modifier new_cred->uid, euid, etc.
// puis remplacer task->cred
```

Approche simplifiee pour cet exercice: on utilise un /proc entry.
L'etudiant ecrit le PID cible, et le module modifie ses credentials.

ATTENTION: modifier les credentials d'un autre processus est dangereux.
En production, les rootkits utilisent plutot des signaux magic (exo 037)
ou des triggers reseau qui agissent dans le contexte du processus cible.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Lis `find_task_by_vpid` dans `kernel/pid.c`:
   https://elixir.bootlin.com/linux/latest/source/kernel/pid.c

2. `find_task_by_vpid` doit etre appele sous `rcu_read_lock()`.

3. `struct task_struct` est dans `include/linux/sched.h`:
   https://elixir.bootlin.com/linux/latest/source/include/linux/sched.h
   Le champ `cred` pointe vers les credentials reelles.

4. Pour modifier les cred d'un autre processus, on peut remplacer
   directement le pointeur `task->cred` (dangereux mais fonctionnel
   pour l'exercice).

## L'exercice

Dans `src/privpid.c`, le /proc entry lit un PID. A toi de:
1. Appeler find_task_by_vpid (sous rcu_read_lock)
2. Creer de nouvelles credentials root
3. Les assigner au processus cible

6 lignes de code.

Compile avec `make`, puis lance `test.sh` dans la VM.

## Test

```bash
./test.sh
```
