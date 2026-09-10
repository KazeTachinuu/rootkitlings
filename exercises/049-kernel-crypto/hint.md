# Hints, exercice 049

## Niveau 1

L'API crypto kernel suit un pattern: alloc transform, set key, alloc request,
set crypt params, execute, free. Chaque etape peut echouer.

N'oublie pas de copier l'IV dans un buffer local car AES-CBC le modifie.

## Niveau 2

```c
struct crypto_skcipher *tfm;
struct skcipher_request *req;
struct scatterlist sg;
u8 iv_copy[AES_BLOCK];

tfm = crypto_alloc_skcipher("cbc(aes)", 0, 0);
crypto_skcipher_setkey(tfm, key, KEY_SIZE);
req = skcipher_request_alloc(tfm, GFP_KERNEL);
```

Puis sg_init_one, memcpy l'IV, skcipher_request_set_crypt, et encrypt/decrypt.

## Niveau 3

```c
static int aes_crypt(const u8 *in, u8 *out, size_t len, const u8 *key,
                     const u8 *iv, bool encrypt)
{
    struct crypto_skcipher *tfm;
    struct skcipher_request *req;
    struct scatterlist sg;
    u8 iv_copy[AES_BLOCK];
    int ret;

    tfm = crypto_alloc_skcipher("cbc(aes)", 0, 0);
    if (IS_ERR(tfm))
        return PTR_ERR(tfm);

    crypto_skcipher_setkey(tfm, key, KEY_SIZE);
    req = skcipher_request_alloc(tfm, GFP_KERNEL);
    if (!req) {
        crypto_free_skcipher(tfm);
        return -ENOMEM;
    }

    memcpy(out, in, len);
    sg_init_one(&sg, out, len);
    memcpy(iv_copy, iv, AES_BLOCK);
    skcipher_request_set_crypt(req, &sg, &sg, len, iv_copy);

    if (encrypt)
        ret = crypto_skcipher_encrypt(req);
    else
        ret = crypto_skcipher_decrypt(req);

    skcipher_request_free(req);
    crypto_free_skcipher(tfm);
    return ret;
}
```
