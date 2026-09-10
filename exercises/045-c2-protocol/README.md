# Exercice 045: definir un protocole C2

VM: qemu (./lab/vm.sh)

## Le mecanisme

Un rootkit ne peut pas envoyer des chaines brutes sur le reseau. Il faut un
protocole structure pour que l'attacking program et le module se comprennent.

Le pattern le plus simple est TLV: Type + Length + Value.

```
+------+--------+-----------------+
| type | length | data            |
| 1 B  | 4 B   | length octets   |
+------+--------+-----------------+
```

Types possibles:
- 0x01: HEARTBEAT (beacon, pas de data)
- 0x02: CMD_REQUEST (data = commande a executer)
- 0x03: CMD_RESPONSE (data = resultat de la commande)
- 0x04: ERROR (data = message d'erreur)

Le length est en network byte order (big-endian, via htonl/ntohl).

Ce n'est pas un standard reseau, c'est un protocole ad hoc. Les vrais C2
(Cobalt Strike, Sliver) utilisent des formats plus complexes (protobuf, HTTPS,
DNS tunneling). Ici on reste simple.

## Ce qu'il faut comprendre AVANT d'ecrire

1. Comprends htonl/ntohl (host to network long) et htons/ntohs:
   ```bash
   man byteorder
   ```

2. Reflechis a l'alignement: le header fait 5 octets (1 + 4).
   En C, on utilise `__attribute__((packed))` pour eviter le padding.

3. Regarde comment le module va parser: il lit d'abord 5 octets (le header),
   puis lit `length` octets supplementaires (les data).

## L'exercice

Dans `src/protocol.h`, les types et la struct sont definis.
Dans `src/c2proto.c`, complete les fonctions `pack_message` et `unpack_header`.

- `pack_message`: prend un type, des data, une longueur, et ecrit le message
  serialise dans un buffer de sortie. Retourne la taille totale.
- `unpack_header`: prend un buffer de 5 octets et extrait le type et la longueur.

Le module charge, se connecte au serveur, envoie un HEARTBEAT (type 0x01,
longueur 0), recoit une CMD_REQUEST, affiche la commande dans dmesg.

## Test

```bash
./test.sh
```

Le test lance test_c2server.py, charge le module, verifie que le module
a parse la commande "id" envoyee par le serveur.
