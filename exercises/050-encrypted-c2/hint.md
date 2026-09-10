# Hints, exercice 050

## Niveau 1

recv_and_decrypt: les 16 premiers octets recus sont l'IV, le reste est le ciphertext.
encrypt_and_send: genere un IV aleatoire, chiffre, prepend l'IV aux data, pack et send.

## Niveau 2

Pour recv_and_decrypt:
```c
u8 *tmp = kmalloc(data_len, GFP_KERNEL);
krecv(s, tmp, data_len);
u8 iv[AES_BLOCK];
memcpy(iv, tmp, AES_BLOCK);
size_t ct_len = data_len - AES_BLOCK;
aes_crypt(tmp + AES_BLOCK, (u8 *)out, ct_len, aes_key, iv, false);
/* Remove PKCS7 padding */
kfree(tmp);
```

Pour encrypt_and_send, utilise get_random_bytes(iv, AES_BLOCK) pour generer l'IV.

## Niveau 3

```c
static int recv_and_decrypt(struct socket *s, char *out, __u32 data_len)
{
    u8 *tmp, iv[AES_BLOCK];
    size_t ct_len;
    u8 pad;

    tmp = kmalloc(data_len, GFP_KERNEL);
    if (!tmp) return -ENOMEM;
    krecv(s, tmp, data_len);

    memcpy(iv, tmp, AES_BLOCK);
    ct_len = data_len - AES_BLOCK;
    aes_crypt(tmp + AES_BLOCK, (u8 *)out, ct_len, aes_key, iv, false);

    pad = out[ct_len - 1];
    if (pad <= AES_BLOCK)
        out[ct_len - pad] = '\0';
    else
        out[ct_len] = '\0';

    kfree(tmp);
    return 0;
}

static int encrypt_and_send(struct socket *s, __u8 msg_type,
                            const char *plain, size_t plain_len)
{
    size_t padded_len, data_total;
    u8 *padded, *encrypted, *data_buf, iv[AES_BLOCK];
    char send_buf[C2_HEADER_SIZE + AES_BLOCK + C2_MAX_DATA];
    int total;
    u8 pad_val;
    size_t i;

    padded_len = ((plain_len + AES_BLOCK - 1) / AES_BLOCK) * AES_BLOCK;
    if (padded_len == plain_len) padded_len += AES_BLOCK;

    padded = kmalloc(padded_len, GFP_KERNEL);
    encrypted = kmalloc(padded_len, GFP_KERNEL);
    data_buf = kmalloc(AES_BLOCK + padded_len, GFP_KERNEL);
    if (!padded || !encrypted || !data_buf) {
        kfree(padded); kfree(encrypted); kfree(data_buf);
        return -ENOMEM;
    }

    memcpy(padded, plain, plain_len);
    pad_val = padded_len - plain_len;
    for (i = plain_len; i < padded_len; i++)
        padded[i] = pad_val;

    get_random_bytes(iv, AES_BLOCK);
    aes_crypt(padded, encrypted, padded_len, aes_key, iv, true);

    memcpy(data_buf, iv, AES_BLOCK);
    memcpy(data_buf + AES_BLOCK, encrypted, padded_len);
    data_total = AES_BLOCK + padded_len;

    total = pack_msg(send_buf, msg_type, (char *)data_buf, data_total);
    ksend(s, send_buf, total);

    kfree(padded);
    kfree(encrypted);
    kfree(data_buf);
    return 0;
}
```
