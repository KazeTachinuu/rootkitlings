# Issue a one-use operator key

An exposed `root` command is not an acceptable control channel.
Require a fresh one-use secret, consume it before attempting the action, and
replace it even when the credential change fails.
Success proves that two writers cannot spend the same secret and that replaying
an old command never grants credentials.

This operator key is a 16-byte **bearer token**: possession authorizes one
attempt, regardless of the writer's identity.
The endpoint accepts `enable`, `disable`, or
`root 0123456789abcdef0123456789abcdef`, each with one optional final newline.
The token text must be exactly 32 lowercase hexadecimal digits; the shown value
is illustrative.

The status record adds:

```text
completed_actions=2
token=0123456789abcdef0123456789abcdef
```

`completed_actions` counts successful credential changes, not matched tokens
whose credential preparation or supplied pre-commit cancellation fails.

## Delivery and representation

`/proc/rootkitlings` is root-owned with mode `0622`.
Root can pass the readable token to a selected writer; an unprivileged task
cannot read it here.
This is bearer authorization, not identity authentication: anyone who learns the
value can race to use it, and permissions do not protect copies in arguments,
pipes, memory, history, or other channels.
`enable` and `disable` remain an unauthenticated operational switch.

Store the token as 16 binary bytes.
At startup, call `wait_for_random_bytes()` and propagate its error, then call
`get_random_bytes()`.
Regenerate an all-zero first value.
The successful wait is required by Linux v7.2's randomness contract.

The supplied snapshot renders with `bin2hex()`.
Before `hex2bin()`, require the exact command length, prefix, and lowercase
hexadecimal alphabet.
Derive all lengths from the token-size constant.
Use the supplied `crypto_memneq()` wrapper for comparison.

## Serialize the transition

One mutex protects the active token and completion counter and serializes gate
observation with the credential action.
Parse into a local byte array before locking.
While holding the mutex:

1. reject disabled actions with `-EACCES`
2. compare the presented and active bytes
3. copy the match, then erase the active token
4. call unchanged `rk_credentials_replace_current()`
5. generate a replacement different from the consumed token
6. increment `completed_actions` only when the action succeeds

Keep the mutex across every step; it may sleep in this process context.
Consume before acting so a matched token becomes stale even when preparation
returns `-ENOMEM` or cancellation returns `-ECANCELED`.
A fresh token must still be available after either failure.
Concurrent users of the old value then fail against the replacement.
Wrong, stale, malformed, and disabled requests do not consume a token.

Use `memzero_explicit()` when active bytes, parsed bytes, the consumed copy,
copied command, and rendered status text leave scope or become obsolete.
Clear every local copy on every exit after creation.
This prevents compiler removal but cannot clear external copies or undo
disclosure.

## Your task

Edit only `control/authorization.c`:

1. implement exact `root TOKEN` parsing in `rk_parse_root_command()`
2. wait for initialized randomness and generate the first token in
   `rk_authorization_start()`
3. implement the mutex-protected gate, comparison, consume-before-action,
   rotation, and accounting in `rk_authorization_execute()`
4. erase every local token copy on every exit

Keep `privilege/credentials.c` unchanged.
The proc boundary, rendering, attribution, credential record, switch, lifecycle,
comparison wrapper, and teardown erasure are supplied.

Run:

```bash
./rootkitlings check
```

The checker proves grammar, permissions, gating, wrong-token and replay
rejection, one winner among concurrent users, same-writer replacement,
preparation and cancellation failures, consume-on-failure rotation, rollback,
clean unload, and repeated load cycles without kernel warnings.

## Official Linux v7.2 references

- [Kernel random-number implementation and readiness contract][ref-1]
- [`get_random_bytes()` and `wait_for_random_bytes()` declarations][ref-2]
- [`hex2bin()` and `bin2hex()`][ref-3]
- [`crypto_memneq()` interface][ref-4]
- [`crypto_memneq()` implementation][ref-5]
- [`memzero_explicit()`][ref-6]
- [Kernel mutex design](https://docs.kernel.org/7.2/locking/mutex-design.html)
- [`mutex_lock()` and `mutex_unlock()`][ref-7]
- [Proc entry creation and removal][ref-8]

[ref-1]:
  https://github.com/torvalds/linux/blob/v7.2/drivers/char/random.c

[ref-2]:
  https://github.com/torvalds/linux/blob/v7.2/include/linux/random.h

[ref-3]:
  https://github.com/torvalds/linux/blob/v7.2/lib/hexdump.c

[ref-4]:
  https://github.com/torvalds/linux/blob/v7.2/include/crypto/utils.h

[ref-5]:
  https://github.com/torvalds/linux/blob/v7.2/lib/crypto/memneq.c

[ref-6]:
  https://github.com/torvalds/linux/blob/v7.2/include/linux/string.h#L347-L365

[ref-7]:
  https://github.com/torvalds/linux/blob/v7.2/kernel/locking/mutex.c

[ref-8]:
  https://github.com/torvalds/linux/blob/v7.2/fs/proc/generic.c
