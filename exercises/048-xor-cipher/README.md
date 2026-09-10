# Exercice 048: chiffrement XOR

VM: qemu (./lab/vm.sh)

## Le mecanisme

XOR est le chiffrement le plus simple: chaque octet du message est XOR
avec un octet de la cle (en boucle). C'est symetrique: XOR une deuxieme
fois avec la meme cle restore le texte original.

```
chiffre[i] = clair[i] ^ cle[i % cle_len]
clair[i]   = chiffre[i] ^ cle[i % cle_len]
```

XOR seul est cryptographiquement faible (vulnerable a l'analyse de frequence
si la cle est courte, et au known-plaintext attack). Mais c'est un bon point
de depart pour comprendre le concept avant de passer a AES (exo 049).

Pour le projet WLKOM, XOR suffit pour la feature "password" (chiffrer le
mot de passe stocke dans le module).

## Ce qu'il faut comprendre AVANT d'ecrire

1. XOR est une operation bit a bit: 0^0=0, 0^1=1, 1^0=1, 1^1=0
2. En C: `a ^ b`
3. Le meme code sert pour chiffrer ET dechiffrer (symetrique)
4. La cle doit etre partagee entre le module et l'attacking program

## L'exercice

Dans `src/xor.c`, complete les fonctions `xor_encrypt` et `xor_decrypt`
(c'est la meme operation, mais on garde deux noms pour la clarte).

Le module:
1. Se connecte au serveur
2. Recoit un message XOR-chiffre
3. Le dechiffre
4. L'affiche dans dmesg
5. Envoie une reponse XOR-chiffree

La cle partagee est "rootkitlings" (definie dans le code).

## Test

```bash
./test.sh
```

Le test lance test_xor_server.py qui envoie un message chiffre et verifie
que la reponse est correctement chiffree.
