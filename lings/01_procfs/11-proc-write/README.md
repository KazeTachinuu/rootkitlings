# 11 - Accept a bounded procfs write

Reading `/proc/rk_store` moves a kernel value to userspace.
Writing it crosses the boundary in the other direction, so the supplied pointer and length are untrusted.

```console
# cat /proc/rk_store
hello
# printf phantom > /proc/rk_store
# cat /proc/rk_store
phantom
```

## Copy, then commit

Never copy userspace bytes directly into live state.
`copy_from_user()` may copy only part of the input before reporting failure.

```text
userspace buffer
       |
       v
local candidate -- copy failed --> discard
       |
   terminate and trim
       |
       v
lock --> replace message --> unlock
```

The local candidate keeps `message` unchanged on failure.
`message_lock` prevents a reader from observing a concurrent replacement halfway through.

## Implement it

Complete `message_write()` in `hello.c`:

1. Reject zero bytes and any input that leaves no room for `\0`.
2. Copy into a local `MESSAGE_SIZE` candidate with `copy_from_user()`.
3. Reject an embedded `\0`; procfs supplies a byte count, not a trusted C string.
4. Terminate the candidate and remove one trailing newline.
5. Lock `message_lock`, copy the candidate into `message`, then unlock.
6. Return the original byte count on success.

Return `-EFAULT` when the userspace copy fails and `-E2BIG` for oversized input.

The proc entry is mode `0600`, so only root can use this control surface.

```sh
./rootkitlings check L11
```

The check writes runtime values, reads them back through procfs, rejects oversized and embedded-NUL input without changing the stored value, and verifies unload cleanup.
