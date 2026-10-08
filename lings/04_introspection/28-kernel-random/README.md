# 28 - Generate kernel randomness

The kernel random API provides values suitable for identifiers, nonces, keys, and unpredictable tokens.
Use it instead of timestamps, counters, or ad-hoc arithmetic when unpredictability matters.

## Observe it

The module generates one 32-bit session ID and one 16-byte token at load time.

```bash
cat /proc/rk_entropy
# session=8 hex digits
# token=32 hex digits
```

Each load asks the kernel random generator for new values.

## How it works

`get_random_u32()` returns a random 32-bit value.
`get_random_bytes(buffer, size)` fills an arbitrary buffer.
Binary data is rendered as two hexadecimal characters per byte for observation only.

Randomness does not provide secrecy after a token is exposed.
Sensitive buffers should not be published through procfs and should be cleared when their lifetime ends.

## Implement it

Generate `session_id` with `get_random_u32()`.
Fill the entire `auth_token` array with `get_random_bytes()`.

## Verify

```bash
./rootkitlings start L28
$EDITOR .rootkitlings/work/kernel-randomness/hello.c
./rootkitlings check L28
```

The check validates that `session` is eight lowercase hexadecimal digits and `token` is 32 hexadecimal characters, then confirms cleanup after unload.
