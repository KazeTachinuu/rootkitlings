# Exercice 002: Module Info

Mode VM: vng (boot rapide)

## Le mecanisme

Chaque module kernel peut embarquer des metadonnees: licence, auteur,
description, version. Ces infos sont stockees dans des sections ELF speciales
du fichier .ko et lisibles sans charger le module, via `modinfo`.

Les macros sont definies dans `include/linux/module.h`:

https://elixir.bootlin.com/linux/latest/source/include/linux/module.h

Cherche `MODULE_INFO` dans ce fichier. Tu verras que MODULE_LICENSE,
MODULE_AUTHOR, MODULE_DESCRIPTION et MODULE_VERSION sont toutes des
wrappers autour de MODULE_INFO qui ecrivent dans la section `.modinfo`
du fichier ELF.

La licence est particulierement importante. Le kernel maintient une liste
de licences "GPL-compatibles" dans `include/linux/license.h`:

https://elixir.bootlin.com/linux/latest/source/include/linux/license.h

Un module sans licence GPL ne peut pas appeler les symboles marques
EXPORT_SYMBOL_GPL. Dans le contexte rootkit, c'est important car beaucoup
de fonctions utiles (comme certaines fonctions ftrace) sont GPL-only.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Sur ta machine, compile le module de l'exo 001 et tape:
   ```bash
   modinfo src/hello.ko
   ```
   Tu verras les champs deja presents (licence, depends, etc.).

2. Dans Elixir Bootlin, cherche la macro MODULE_AUTHOR. Note qu'elle
   appelle simplement MODULE_INFO(author, ...).

3. Tape `modinfo` sur un module systeme, par exemple:
   ```bash
   modinfo /lib/modules/$(uname -r)/kernel/drivers/net/tun.ko
   ```
   Regarde les champs: description, author, license, version, etc.

## L'exercice

Dans `src/info.c`, le module compile et fonctionne deja (init et exit
sont ecrits). Ajoute les 4 macros de metadonnees aux TODO.

Apres `make`, lance `modinfo src/info.ko` et verifie que tous les champs
apparaissent. Puis lance `test.sh` dans la VM.

## Test

```bash
./test.sh
```

Le test verifie que modinfo affiche les 4 champs attendus.
