# Provision and rotate a shared key

The AEAD and envelope layers need a 256-bit key. Load the initial
key from a module parameter at insmod time, and provides mkdir-based commands
to rotate the key at runtime without reloading the module.

## Key lifecycle

```text
insmod rklings.ko control_key="<64 hex chars>"
        |
        v
  module_param_string receives hex string
        |
        v
  hex2bin: 64 hex chars -> 32 bytes
        |
        v
  memzero_explicit: wipe hex string from memory
        |
        v
  key is live in rk_key[32]
        |
        +--- mkdir /tmp/key.rotate  -> get_random_bytes(rk_key, 32)
        |
        +--- mkdir /tmp/key.<hex>   -> hex2bin new key into rk_key
        |
        v
  rmmod: memzero_explicit(rk_key)
```

## Module parameter

`module_param_string` stores the parameter in a fixed-size buffer rather than
a heap pointer, making it possible to zeroize after parsing:

```c
static char rk_key_hex[65];
module_param_string(control_key, rk_key_hex, sizeof(rk_key_hex), 0);
```

The `0` permission hides the parameter from sysfs. After parsing with hex2bin,
wipe the hex string immediately.

## Mkdir commands

The mkdir hook intercepts directory creation on /tmp. Commands:

- `key.rotate`: overwrite rk_key with 32 cryptographically random bytes from
  `get_random_bytes`. Return -EEXIST to suppress directory creation.
- `key.<64 hex>`: parse the hex suffix with hex2bin into rk_key. Return
  -EINVAL on bad hex, -EEXIST on success.

Any name not starting with "key." passes through to the original mkdir.

## Key zeroization

On module unload, wipe the key with `memzero_explicit`. This function is
guaranteed not to be optimized away by the compiler, unlike a plain memset.

## Demo

```sh
# Generate a random key and load the module
insmod rklings.ko control_key=$(head -c32 /dev/urandom | xxd -p -c64)

# Rotate to a new random key at runtime
mkdir /tmp/key.rotate

# Load a specific key at runtime
mkdir /tmp/key.$(head -c32 /dev/urandom | xxd -p -c64)
```

## Your task

Edit `main.c`:

1. Declare `control_key` with `module_param_string`, permission 0.
2. In `rk_init`: validate hex length, parse with hex2bin, zeroize hex string.
3. Hook /tmp mkdir with `kern_path` and inode operation replacement.
4. Implement `rk_handle_mkdir` to dispatch key.rotate and key.<hex> commands.
5. In `rk_exit`: restore the original mkdir, zeroize rk_key.

Run:

```sh
./rklings check
```

## References

- [`module_param_string`](https://elixir.bootlin.com/linux/v7.2/source/include/linux/moduleparam.h)
- [`hex2bin`](https://elixir.bootlin.com/linux/v7.2/source/lib/hexdump.c)
- [`memzero_explicit`](https://elixir.bootlin.com/linux/v7.2/source/include/linux/string.h)
- [`get_random_bytes`](https://elixir.bootlin.com/linux/v7.2/source/include/linux/random.h)
