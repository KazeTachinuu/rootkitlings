# 57 - XOR obfuscation

## What we are learning

Rootkits obfuscate strings (magic words, C2 addresses, command names)
to avoid detection by static analysis tools. XOR is the simplest
cipher: apply the same key byte to encrypt and decrypt. It is trivially
breakable, but enough to defeat `strings` and basic signature scanning.

This is the first step before real encryption (ling 58).

## The concept

```
  XOR is symmetric: encrypt = decrypt

  plaintext:   r    o    o    t    k    i    t
  hex:        72   6F   6F   74   6B   69   74
  XOR 0x42:   30   2D   2D   36   29   2B   36
  cipher:     0x30 2D 2D 36 29 2B 36

  decrypt:    30   2D   2D   36   29   2B   36
  XOR 0x42:   72   6F   6F   74   6B   69   74
  plaintext:   r    o    o    t    k    i    t
```

Applying the same key twice recovers the original. Trivially breakable,
but defeats `strings` and basic signature scanning.

## What we are doing

Create a module that stores a plaintext string, XOR-encrypts it with a
single-byte key (0x42), and exposes the plain text, hex-encoded cipher,
and round-trip decryption via `/proc/rk_xor`. Writing to `/proc/rk_xor`
sets a new plaintext.

## Includes you need

```c
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
```

## Key code

The XOR function is symmetric (encrypt = decrypt):

```c
#define XOR_KEY 0x42

static void xor_buf(const char *in, char *out, int len, u8 key)
{
    int i;
    for (i = 0; i < len; i++)
        out[i] = in[i] ^ key;
}
```

To display the cipher as hex in the proc read:

```c
for (i = 0; i < len; i++)
    off += snprintf(buf + off, sizeof(buf) - off, "%02x", cipher[i]);
```

## Output format

```
plain=rootkit secret data
cipher=30272D2B...
decrypt=rootkit secret data
```

The cipher line shows the XOR-encrypted bytes in hexadecimal.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_xor                 # plain, xored (hex), and decoded fields
dmesg | tail
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_xor` contains `plain=`, `cipher=`, and `decrypt=` fields.
- Round-trip: `decrypt` must equal `plain`.
- Writing new plaintext updates the round-trip with the new input.
- `/proc/rk_xor` disappears after unload.

## Verify

```bash
./rootkitlings check 57
```

The check verifies the round-trip: write a new plaintext, read it back,
confirm the decrypted text matches.

> **What a defender sees**: `strings hello.ko` will not show the
> obfuscated data. But XOR with a fixed key is trivially reversible:
> tools like `xortool` automate the attack.

See also: [58 - AES encryption](../58-aes-encryption/README.md).
