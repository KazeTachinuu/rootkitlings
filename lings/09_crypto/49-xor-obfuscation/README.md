# 49 - Obfuscate a static string with XOR

Printable strings embedded in a module are easy to extract without loading it:

```sh
./rootkitlings start L49
make --quiet
strings module/hello.ko
```

A fixed command, path, or server name can therefore become a simple signature.
One way to remove the plaintext from the file is to store transformed bytes and recover the string only when it is needed.

```text
module on disk                         module at runtime
-------------------------------        --------------------
30 2d 2d 36 29 2b 36 ...   --XOR-->   rootkit secret data
        no plaintext                    plaintext in memory
```

This is obfuscation, not secure encryption.
Anyone who finds the bytes and key can reverse it.

## XOR

XOR compares corresponding bits.
Applying the same key twice restores the original value:

```text
value ^ key = encoded
encoded ^ key = value
```

For the first character:

```text
'r'       = 0x72
key       = 0x42
0x72^0x42 = 0x30
0x30^0x42 = 0x72 = 'r'
```

The module stores the complete string as encoded bytes:

```text
plain   = rootkit secret data
cipher  = 302d2d36292b36623127213027366226233623
```

## Transform the buffer

Start from the supplied implementation:

`xor_buf()` currently copies every byte unchanged:

```c
output[i] = input[i];
```

Apply the key instead:

```c
output[i] = input[i] ^ key;
```

The same function now performs both directions:

```text
plaintext  --xor_buf--> ciphertext
ciphertext --xor_buf--> plaintext
```

`/proc/rk_xor` shows the stored bytes and their runtime decoding.
Writing a new value encodes it before storing it:

```sh
printf test123 > /proc/rk_xor
cat /proc/rk_xor
```

```text
cipher=36273136737071
plain=test123
```

## Verify

```sh
./rootkitlings check L49
```

The host check confirms that `rootkit secret data` is absent from printable strings in `hello.ko`.
The VM checks the exact encoded bytes, runtime decoding, a new value written through procfs, and cleanup after unload.

## Limits

The decoded text exists in kernel memory while the module runs.
A fixed one-byte key is easy to recover from repeated patterns or known plaintext.
XOR can conceal an obvious static string, but it provides neither secrecy nor integrity.
