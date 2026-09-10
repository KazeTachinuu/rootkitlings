# Deepdive 048: XOR cipher

## Question ouverte

XOR avec une cle courte est casse trivalement par analyse de frequence.
Si un attaquant connait un bout du plaintext (known-plaintext attack),
il peut retrouver la cle:

```
cle[i] = chiffre[i] ^ plaintext_connu[i]
```

Par exemple, si l'attaquant sait que le premier message est toujours
"HEARTBEAT", il retrouve les 9 premiers octets de la cle.

Comment rendrais-tu XOR plus resistant ?

Pistes:
- Cle aussi longue que le message (one-time pad, theoriquement incassable)
- Deriver une cle unique par message (nonce + KDF)
- Oublier XOR et utiliser un vrai chiffrement (AES)

L'exercice 049 utilise la kernel crypto API pour AES-CBC.
