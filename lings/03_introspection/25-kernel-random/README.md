# 25 - Kernel random

## Why this step

Predictable values are fingerprintable. A beacon at exactly 60s intervals
is an IDS signature. A fixed magic number is a Yara rule. The kernel's
CSPRNG gives unpredictable bytes for jitter, nonces, and identifiers.

## What you need

You already know `proc_create` and `snprintf` from ling 08. This ling
generates random values at load time.

New includes:

```c
#include <linux/random.h>   // get_random_u32, get_random_bytes
```

## Build it

Create a module where:

1. On load, generate a random 32-bit session ID with `get_random_u32`.
2. Generate 16 random bytes as a hex token with `get_random_bytes`.
3. `/proc/rk_entropy` returns both values.
4. Each load produces different output.

The two APIs:

```c
u32 val = get_random_u32();           // one random 32-bit int
get_random_bytes(buf, sizeof(buf));   // fill a byte array
```

Generate both at init time (module globals). In the read callback,
format them as hex: `%08x` for the session ID, `%02x` per byte for
the token. Use the snprintf pattern from ling 08.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_entropy       # session=a1b2c3d4  token=...
rmmod hello
insmod hello.ko
cat /proc/rk_entropy       # different values
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_entropy` must output lines in this format:
  ```
  session=HEXVALUE
  token=HEXVALUE
  ```
- `session=` is an 8-hex-char random value (non-zero).
- `token=` is a 32-hex-char random value.
- Both change on each module reload (proving randomness).

## Verify

```bash
./rootkitlings check 25
```

> **What a defender sees**: the randomness makes each session unique.
> Session IDs and jitter values change on every load. Pattern matching
> becomes harder.

See also: [Hooking](../../04_hooking/README.md).
