# Authorize one credential change with a fresh token

Protect the previous exercise's `root` action with one 16-byte bearer token.
The proc endpoint accepts:

```text
enable
disable
root 0123456789abcdef0123456789abcdef
```

The example token is illustrative.
The module generates the real value.
One final newline is optional.
Token text is exactly 32 lowercase hexadecimal digits.

The status record adds two lines:

```text
active=1
actions=enabled
reader_tid=123
reader_comm="cat"
uids=0 0 0 0
gids=0 0 0 0
cap_userns=initial
cap_permitted=000001ffffffffff
cap_effective=000001ffffffffff
completed_actions=2
token=0123456789abcdef0123456789abcdef
```

`completed_actions` counts successful credential changes.
It does not count a matched token when credential preparation fails or the supplied pre-commit cancellation runs.

## Token delivery

`/proc/rootkitlings` is owned by root with mode `0622`:

- root can read the current token
- any task can write a command
- an unprivileged task cannot obtain the token by reading this proc entry

A root process can read one token and give it to a selected unprivileged process.
The writing process then presents the token with the `root` command.

This is bearer authorization, not authentication.
The module does not prove who presents the token.
Any process that obtains it can race to use it.
The proc permissions protect only this read path; they do not protect copies in arguments, pipes, process memory, terminal history, or other channels.

The `enable` and `disable` commands remain an operational switch.
Any writer can change that switch.
It is not an authorization decision.

## Generate binary state, render text at the boundary

Store the active token as 16 bytes.
Do not store or compare its hexadecimal rendering.

At authorization startup:

1. call `wait_for_random_bytes()` and propagate its error
2. call `get_random_bytes()` only after that wait succeeds
3. reject an all-zero first value by generating again

Linux v7.2 documents that callers must successfully wait for RNG initialization at least once to retain the secure-randomness guarantee of `get_random_bytes()`.

The supplied status snapshot uses `bin2hex()` to render the bytes.
Parse a command with `hex2bin()` only after checking its exact length, prefix, and lowercase hexadecimal alphabet.
All buffer lengths are derived from the one 16-byte token constant.

The supplied comparison wrapper uses `crypto_memneq()`.
It avoids a content-dependent comparison without asking this exercise to implement a cryptographic comparison routine.

## Serialize one complete transition

One mutex protects the active token and completed-action counter.
It also serializes the gate observation and the credential action.

Parse into a local byte array before taking the mutex.
Then, while holding the mutex:

1. reject the request if actions are disabled
2. compare the presented bytes with the active bytes
3. copy the matched value, then erase the active token
4. call the unchanged `rk_credentials_replace_current()` function
5. generate a replacement different from the consumed token
6. increment `completed_actions` only if the credential action succeeded

Keep the mutex held through every step.
Kernel mutexes are sleep-capable, so the credential allocation may run inside this process-context critical section.

Erasing before the action makes consumption unambiguous.
If credential preparation returns `ENOMEM`, or the supplied cancellation path returns `ECANCELED`, that token is still stale and a fresh token is available.
A concurrent waiter holding the old value compares against the replacement and is rejected.

Wrong, stale, malformed, and disabled requests do not consume the token.

## Erasure has a limited scope

Use `memzero_explicit()` for the active bytes, parsed bytes, consumed copy, copied command, and rendered status text when each is no longer needed.
Unlike ordinary `memset()`, this call is not removed as a dead store by the compiler.

This clears only those specific storage locations.
It cannot erase copies already placed in a seq-file buffer or userspace, and it cannot recover a token that has been disclosed.
Rotation limits reuse; it does not undo disclosure.

## Your task

Edit only `control/authorization.c`.

1. implement the exact `root TOKEN` parser in `rk_parse_root_command()`
2. wait for initialized randomness and generate the first token in
   `rk_authorization_start()`
3. implement the mutex-protected gate, comparison, consume-before-action,
   rotation, and success accounting in `rk_authorization_execute()`
4. clear every local token copy on every exit after it is created

Keep `privilege/credentials.c` unchanged.
The proc copy boundary, status rendering, task attribution, credential record, control switch, lifecycle, constant-time comparison wrapper, and teardown erasure are supplied.

Run:

```bash
./rootkitlings check
```

The checker covers the exact grammar and status shape, proc permissions, disabled and wrong tokens, replay, simultaneous same-token writers, successful same-caller replacement, sibling isolation, preparation and cancellation failures, consumption on failure, publication rollback, held-descriptor unload, two load cycles, and kernel warnings.

## Official Linux v7.2 references

- [Kernel random-number implementation and readiness contract](https://github.com/torvalds/linux/blob/v7.2/drivers/char/random.c)
- [`get_random_bytes()` and `wait_for_random_bytes()` declarations](https://github.com/torvalds/linux/blob/v7.2/include/linux/random.h)
- [`hex2bin()` and `bin2hex()`](https://github.com/torvalds/linux/blob/v7.2/lib/hexdump.c)
- [`crypto_memneq()` interface](https://github.com/torvalds/linux/blob/v7.2/include/crypto/utils.h)
- [`crypto_memneq()` implementation](https://github.com/torvalds/linux/blob/v7.2/lib/crypto/memneq.c)
- [`memzero_explicit()`](https://github.com/torvalds/linux/blob/v7.2/include/linux/string.h#L347-L365)
- [Kernel mutex design](https://docs.kernel.org/7.2/locking/mutex-design.html)
- [`mutex_lock()` and `mutex_unlock()`](https://github.com/torvalds/linux/blob/v7.2/kernel/locking/mutex.c)
- [Proc entry creation and removal](https://github.com/torvalds/linux/blob/v7.2/fs/proc/generic.c)
