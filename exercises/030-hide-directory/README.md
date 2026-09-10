# Exercice 030: hide a directory

VM: qemu (./lab/vm.sh)

## Le mecanisme

L'exercice 029 cache les fichiers par prefixe. Ici on cache un dossier
specifique par son nom exact. Le mecanisme est identique: hook getdents64,
filtrer le buffer.

La difference: au lieu de `strncmp` avec un prefixe, on fait `strcmp`
avec un nom exact. Le dossier qu'on cache est le "repaire" du rootkit,
par convention `.rootkit_lair`.

Le point (.) rend le dossier deja invisible a `ls` (sans -a), mais `ls -la`
le montre. Notre hook le cache meme de `ls -la`.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Meme mecanisme que 029 (getdents64 hook, buffer parsing).

2. La seule difference est la condition de match: `strcmp` au lieu de `strncmp`.

3. Le nom du dossier est configure par un `#define` ou un `module_param`.

## L'exercice

Dans `src/hidedir.c`, le hook getdents64 est en place. Complete la condition
de filtrage pour cacher les entrees dont le nom correspond exactement
a HIDE_DIR.

4 lignes de code dans la boucle de filtrage.

Compile avec `make`, puis lance `test.sh` dans la VM.

## Test

```bash
./test.sh
```
