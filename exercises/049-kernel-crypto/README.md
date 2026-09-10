# Exercice 049: kernel crypto API (AES-CBC)

VM: qemu (./lab/vm.sh)

## Le mecanisme

Le kernel Linux a une API crypto complete, utilisee par IPsec, dm-crypt, etc.
Pour le chiffrement symetrique, on utilise l'API "skcipher" (symmetric key cipher).

Les etapes:
1. Allouer un transform: `crypto_alloc_skcipher("cbc(aes)", 0, 0)`
2. Definir la cle: `crypto_skcipher_setkey(tfm, key, key_len)`
3. Allouer une requete: `skcipher_request_alloc(tfm, GFP_KERNEL)`
4. Preparer le scatterlist avec les donnees
5. Definir l'IV (initialization vector)
6. Lancer: `crypto_skcipher_encrypt(req)` ou `crypto_skcipher_decrypt(req)`

AES-CBC (Cipher Block Chaining) chiffre par blocs de 16 octets. Le plaintext
doit etre un multiple de 16 (padding si necessaire). L'IV fait 16 octets.

Source:
- https://elixir.bootlin.com/linux/latest/source/include/crypto/skcipher.h
- https://elixir.bootlin.com/linux/latest/source/Documentation/crypto/api-skcipher.rst

## Ce qu'il faut comprendre AVANT d'ecrire

1. Regarde les includes: `<crypto/skcipher.h>`, `<linux/scatterlist.h>`

2. Un scatterlist decrit un buffer en memoire pour le DMA/crypto:
   ```c
   struct scatterlist sg;
   sg_init_one(&sg, data, data_len);
   ```

3. L'IV doit etre aleatoire pour chaque message en production.
   Ici on utilise un IV fixe pour simplifier le test.

4. Le padding PKCS7: on ajoute N octets de valeur N pour atteindre
   un multiple de 16. Ex: "hello" (5 octets) + 11 octets de valeur 0x0B.

## L'exercice

Dans `src/kcrypto.c`, complete les fonctions `aes_encrypt` et `aes_decrypt`.

Le module cree /proc/rootkitlings_crypto. Quand on ecrit du texte, il le
chiffre avec AES-CBC, puis le dechiffre, et affiche le resultat dans dmesg
pour verifier le roundtrip.

## Test

```bash
./test.sh
```

Le test charge le module, ecrit "test message 123" dans /proc/rootkitlings_crypto,
et verifie que dmesg montre le roundtrip correct.
