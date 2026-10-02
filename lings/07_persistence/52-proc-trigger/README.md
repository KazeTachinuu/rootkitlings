# 52 - Proc trigger

## What we are learning

Rootkits need a way for the operator to activate features: escalate
privileges, toggle hiding, update configuration. A writable `/proc`
entry that accepts a magic word is the simplest trigger mechanism.

This is how Diamorphine activates: write a magic string, get root.

## What we are doing

Combine proc write (ling 12) with credential escalation (ling 26) into
a command interface:

```
  user (any UID)
       |
  echo "r00tme" > /proc/rk_privesc
       |
  +----------------------------+
  | proc write handler         |
  |   "r00tme"? --> escalate   |
  |   anything else? --> noop  |
  +----------------------------+
       |
  user is now UID 0
       |
  cat /proc/rk_privesc --> "triggers=1 uid=0"
```

## Build it

You already have both pieces from earlier lings:

- **Proc write callback** (ling 12): `copy_from_user` the input,
  null-terminate it, compare against the magic word.
- **Credential escalation** (ling 26): `prepare_creds()`, set
  `uid`/`euid` to `GLOBAL_ROOT_UID`, `commit_creds()`.

New considerations:

1. Create `/proc/rk_privesc` with `0666` permissions so any user can write.
2. The magic word is `r00tme` (no newline, no trailing whitespace).
3. Track activations with an atomic counter.
4. The read callback shows both the trigger count and the UID of
   whoever is reading (proving escalation worked).

**Watch out**: `echo` appends a newline. Decide how your comparison
handles that. The check writes the exact string `r00tme` without a
trailing newline.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_privesc                       # triggers=0, uid shows current user
printf 'r00tme' > /proc/rk_privesc         # trigger the escalation
cat /proc/rk_privesc                       # triggers=1, uid=0
id                                    # should be root now
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_privesc` contains `triggers=N` and `uid=N`.
- Writing `r00tme` escalates the caller to root and increments `triggers`.
- Writing anything else does NOT trigger escalation.
- `/proc/rk_privesc` disappears after unload.

## Verify

```bash
./rootkitlings check 52
```

The check writes the magic word, verifies the trigger counter
incremented, then writes a wrong word and verifies it was rejected.

> **What a defender sees**: a world-writable `/proc` entry is suspicious.
> `ls -la /proc/rk_privesc` shows 0666 permissions. The magic word appears in
> plaintext in the module binary (`strings hello.ko`).

See also: [53 - Usermode helper](../53-init-hooks/README.md).
