# Hints, exercice 048

## Niveau 1

XOR est une boucle simple. L'operateur est ^.
La cle se repete: utilise l'operateur modulo (%).

## Niveau 2

```c
for (size_t i = 0; i < len; i++)
    buf[i] ^= key[i % key_len];
```

## Niveau 3

```c
static void xor_encrypt(char *buf, size_t len, const char *key, size_t key_len)
{
    size_t i;
    for (i = 0; i < len; i++)
        buf[i] ^= key[i % key_len];
}

static void xor_decrypt(char *buf, size_t len, const char *key, size_t key_len)
{
    xor_encrypt(buf, len, key, key_len);
}
```
