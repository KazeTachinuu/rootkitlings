# Exercice 003: Module Parameters

Mode VM: vng (boot rapide)

## Le mecanisme

Un module kernel peut accepter des parametres au chargement, comme des
arguments de ligne de commande. Le mecanisme est defini dans
`include/linux/moduleparam.h`:

https://elixir.bootlin.com/linux/latest/source/include/linux/moduleparam.h

La macro `module_param(name, type, perm)` declare un parametre:
- `name`: le nom de la variable C (doit exister comme variable globale)
- `type`: le type (int, charp pour char*, bool, uint, etc.)
- `perm`: les permissions du fichier dans /sys/module/NOM/parameters/

Les types supportes sont definis dans le meme fichier. Cherche
`module_param_named` et les fonctions `param_set_*` / `param_get_*`.

Les permissions (`perm`) controlent si le parametre est visible et
modifiable depuis /sys. Par exemple:
- 0: pas visible dans /sys
- 0444: lecture seule
- 0644: lecture + ecriture par root

Quand perm > 0, le parametre apparait dans /sys/module/NOM/parameters/
et peut etre modifie en live (sans recharger le module).

## Ce qu'il faut comprendre AVANT d'ecrire

1. Ouvre Elixir Bootlin et lis la definition de `module_param`:
   https://elixir.bootlin.com/linux/latest/source/include/linux/moduleparam.h

2. Charge un module systeme avec des parametres. Par exemple:
   ```bash
   modinfo /lib/modules/$(uname -r)/kernel/drivers/net/tun.ko
   ```
   Regarde la section "parm:" dans la sortie de modinfo.

3. Comprends la syntaxe d'insmod avec parametres:
   ```bash
   insmod monmodule.ko monparam=42
   ```

## L'exercice

Dans `src/params.c`, declare deux parametres:
1. Un entier `myint` avec une valeur par defaut de 0
2. Une chaine `mystr` avec une valeur par defaut "world"

La fonction init affiche les valeurs recues. Complete les TODO.

## Test

```bash
./test.sh
```

Le test charge le module avec des valeurs specifiques et verifie
que dmesg affiche les bonnes valeurs.
