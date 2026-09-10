# Exercice 050: C2 chiffre

VM: qemu (./lab/vm.sh)

## Le mecanisme

Cet exercice est le culminant de rootkitlings. On combine:
- La boucle C2 (exo 047)
- Le protocole TLV (exo 045)
- Le chiffrement AES-CBC (exo 049)

Le flow modifie:
1. Le module se connecte au serveur
2. Il envoie un HEARTBEAT (en clair, c'est juste un header de 5 octets)
3. Il recoit une CMD_REQUEST dont les DATA sont chiffrees en AES-CBC
4. Il dechiffre les data pour obtenir la commande
5. Il execute la commande
6. Il chiffre le resultat
7. Il envoie une CMD_RESPONSE avec les data chiffrees

L'IV est prepend au champ data: les 16 premiers octets sont l'IV,
le reste est le ciphertext.

```
TLV header (5 B) + IV (16 B) + ciphertext (N B, multiple de 16)
```

## Ce qu'il faut comprendre AVANT d'ecrire

1. Revois l'exo 047 (boucle C2) et 049 (AES).
2. L'IV est genere par le serveur pour les CMD_REQUEST et par le module
   pour les CMD_RESPONSE (chacun genere son IV).
3. get_random_bytes(iv, 16) genere un IV aleatoire dans le kernel.
4. Le padding PKCS7 est necessaire avant le chiffrement.

## L'exercice

Dans `src/cryptoc2.c`, l'ossature est fournie (connect, boucle, send/recv,
protocole, AES). Complete la fonction `recv_and_decrypt` et `encrypt_and_send`
qui wrappent recv/send avec le chiffrement.

## Test

```bash
./test.sh
```

Le test lance test_crypto_c2server.py, qui parle le protocole TLV avec
chiffrement AES-CBC. Le serveur envoie "whoami" chiffre, le module dechiffre,
execute, chiffre le resultat, et l'envoie. Le serveur verifie.
