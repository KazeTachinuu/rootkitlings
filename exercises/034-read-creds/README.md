# Exercice 034: read process credentials from kernel

VM: qemu (./lab/vm.sh)

## Le mecanisme

Chaque processus Linux a des credentials (identifiants) qui definissent
ses permissions: uid, gid, euid, egid, etc. Cote kernel, c'est la
structure `struct cred` definie dans `include/linux/cred.h`.

Le processus courant (celui qui a provoque le chargement du module) est
accessible via la macro `current`. Ses credentials sont dans `current->cred`.

Pour lire les credentials de facon safe:

```c
const struct cred *cred = current_cred();
uid_t uid = cred->uid.val;
uid_t euid = cred->euid.val;
```

`current_cred()` retourne un pointeur const vers les credentials du
processus courant. Le `.val` est necessaire parce que uid/euid sont
de type `kuid_t` (un wrapper autour de uid_t).

Quand un processus a uid=0, il est root. L'exercice 035 montrera comment
changer ces credentials.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Lis `struct cred` dans `include/linux/cred.h`:
   https://elixir.bootlin.com/linux/latest/source/include/linux/cred.h

2. Repere les champs: uid, gid, suid, sgid, euid, egid, fsuid, fsgid.
   Chaque champ a un role different (real, effective, saved, filesystem).

3. Lis la macro `current_cred()` dans le meme fichier. Elle retourne
   `current->cred` de facon RCU-safe.

4. `current` est defini dans `include/asm-generic/current.h`. C'est un
   pointeur vers le `struct task_struct` du processus en cours d'execution.

## L'exercice

Dans `src/readcreds.c`, complete le module_init pour:
1. Obtenir les credentials du processus courant
2. Afficher uid, euid, gid, egid via pr_info

4 lignes de code.

Compile avec `make`, puis lance `test.sh` dans la VM.

## Test

```bash
./test.sh
```
