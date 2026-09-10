# Deepdive 049: kernel crypto API

## Question ouverte

On utilise un IV fixe ici, ce qui est une erreur cryptographique grave.
Avec un IV fixe, deux plaintexts identiques donnent le meme ciphertext,
ce qui permet une analyse par correspondance.

En production, l'IV doit etre:
- Unique par message (nonce)
- Imprevisible (genere par get_random_bytes dans le kernel)
- Transmis en clair avec le ciphertext (l'IV n'est pas secret)

Comment modifierais-tu le protocole C2 de l'exo 045 pour transmettre l'IV ?

Piste: ajoute un type C2_IV (0x05) envoye avant chaque message chiffre,
ou prepend l'IV (16 octets) au debut du champ data.

Autre question: AES-CBC est vulnerable au padding oracle attack.
Cherche AES-GCM (Galois/Counter Mode), qui combine chiffrement et
authentification. Le kernel le supporte: `crypto_alloc_aead("gcm(aes)", 0, 0)`.

L'exercice 050 integre le chiffrement dans la boucle C2.
