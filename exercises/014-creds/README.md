# Exercice 014: struct cred, lire les credentials

## Le mecanisme

Chaque processus a des credentials qui determinent ce qu'il peut faire.
La structure `struct cred` contient les UIDs et GIDs:

```c
struct cred {
    kuid_t uid;     // real UID
    kgid_t gid;     // real GID
    kuid_t suid;    // saved UID
    ksgid_t sgid;   // saved GID
    kuid_t euid;    // effective UID (celui qui compte pour les permissions)
    kgid_t egid;    // effective GID
    // ... capabilities, keyrings, etc.
};
```

La task_struct pointe vers les credentials via deux pointeurs:
- `real_cred` : les credentials "reelles" (rarement modifiees)
- `cred` : les credentials "effectives" (celles qui comptent)

Pour LIRE les credentials, on utilise `current_cred()` qui retourne un
pointeur const. Pour les MODIFIER, il faut une procedure speciale
(prepare_creds/commit_creds), qu'on verra a l'exercice 035.

Defini dans:
- `include/linux/cred.h` : struct cred, current_cred()
  https://elixir.bootlin.com/linux/latest/source/include/linux/cred.h
- `kernel/cred.c` : implementation de prepare_creds, commit_creds
  https://elixir.bootlin.com/linux/latest/source/kernel/cred.c

Pour extraire la valeur numerique d'un kuid_t:
```c
uid_t uid_val = __kuid_val(cred->uid);
// ou:
uid_t uid_val = from_kuid(&init_user_ns, cred->uid);
```

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre struct cred sur Elixir Bootlin:
   https://elixir.bootlin.com/linux/latest/source/include/linux/cred.h
   Cherche `struct cred {` et note les differents UIDs (uid, euid, suid, fsuid).

2. Regarde `current_cred()` dans le meme fichier. C'est un wrapper autour
   de `current->cred` avec des annotations RCU (Read-Copy-Update).

3. Note que kuid_t n'est PAS un simple int. C'est un wrapper type-safe.
   Il faut utiliser __kuid_val() ou from_kuid() pour obtenir la valeur.

## L'exercice

Dans `src/creds.c`, complete module_init pour afficher l'UID et le GID
du processus courant.

```bash
make
# Dans la VM:
insmod src/creds.ko       # en root: devrait afficher uid=0
dmesg | tail
rmmod creds
```

## Test

```bash
./test.sh
```
