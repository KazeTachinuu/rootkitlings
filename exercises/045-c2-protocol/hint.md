# Hints, exercice 045

## Niveau 1

pack_message: le header fait 5 octets. Le premier est le type brut.
Les 4 suivants sont la longueur en big-endian (htonl).

unpack_header: c'est l'inverse. Le type est buf[0], la longueur est les
4 octets suivants convertis avec ntohl.

## Niveau 2

Pour pack_message:
```c
out[0] = type;
__be32 net_len = htonl(data_len);
memcpy(out + 1, &net_len, 4);
```

Pour unpack_header:
```c
*type = buf[0];
__be32 net_len;
memcpy(&net_len, buf + 1, 4);
*length = ntohl(net_len);
```

## Niveau 3

```c
static int pack_message(char *out, __u8 type, const char *data, __u32 data_len)
{
    __be32 net_len = htonl(data_len);
    out[0] = type;
    memcpy(out + 1, &net_len, 4);
    if (data_len > 0 && data)
        memcpy(out + C2_HEADER_SIZE, data, data_len);
    return C2_HEADER_SIZE + data_len;
}

static int unpack_header(const char *buf, __u8 *type, __u32 *length)
{
    __be32 net_len;
    *type = buf[0];
    memcpy(&net_len, buf + 1, 4);
    *length = ntohl(net_len);
    return 0;
}
```
