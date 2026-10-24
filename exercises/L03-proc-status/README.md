# L03: Expose Module State in `/proc`

## Mission

Procfs is how the kernel exposes information to userspace.
Understanding it now is what lets the rootkit filter it later.

L02 remembers whether the module is active, but only the load message shows it.

Expose that value through `/proc/rklings` so the shell can read the current
state at any time.

## What you will observe

```text
module state       /proc/rklings       after unload
------------       --------------      ---------------------
active             active=1            path does not exist
inactive           active=0            path does not exist
```

The entry is read-only and exists only while its backing state is alive.

## Workspace

```text
exercises/L03-proc-status
├── core
│   ├── main.c
│   └── state.c
├── include
│   └── rootkit.h
├── interface
│   └── proc.c
└── Kbuild
```

Edit:

- `interface/proc.c` creates, reads, and removes `/proc/rklings`.
- `core/main.c` connects that entry to module load and unload.

The state code, shared header, and build rules are supplied.

## Read live kernel data

In L00, `lsmod` showed that `rklings` was loaded.
It gets that information from the kernel, including through `/proc/modules`:

```console
root@victim:~# lsmod | grep '^rklings '
rklings                12288  0
root@victim:~# grep '^rklings ' /proc/modules
rklings 12288 0 - Live 0x0000000000000000 (O)
```

`/proc` looks like a directory of files, but those files are not stored on disk.
The kernel produces their contents when a program reads them.

You can see this with `/proc/uptime`:

```console
root@victim:~# cat /proc/uptime
184.52 720.31
root@victim:~# cat /proc/uptime
186.08 726.14
```

Each `cat` asks the kernel for the current value.
This exercise adds `/proc/rklings` in the same way.

## Generate a virtual file

When `cat` reads `/proc/rklings`, procfs calls the function registered by the
module:

```text
cat /proc/rklings
        |
        v
rk_status_show(output, unused)
        |
        ├── rk_state_is_active(proc_state)
        └── seq_printf(output, "active=%u\n", value)
        v
cat receives active=1\n
```

`seq_printf` writes formatted text into the callback's `struct seq_file` output
buffer, and returning `0` reports success.

The supplied `rk_proc_create` helper creates `/proc/rklings` with mode `0444`.
It also includes the injectable failure point that validation uses.

## Remove the proc entry before its state

The proc callback reads module state, so the two lifetimes have a strict order:

```text
load                                unload
----                                ------
create state                        remove proc entry
     |                                   |
     v                                   v
publish proc entry                  destroy state
```

If publication fails after state creation, initialization must destroy the state
and return the publication error.
A failed load must leave neither the module nor `/proc/rklings` behind.

## TODOs

```text
TODO 1  interface/proc.c:14:2  write the stored active state to the read buffer
TODO 2  interface/proc.c:27:2  publish the proc entry for one state object
TODO 3  interface/proc.c:33:2  remove the entry and clear its state reference
TODO 4  core/main.c:32:2       publish after state creation or roll state back
TODO 5  core/main.c:39:2       remove the interface before destroying state
```

### TODO 1 -- `interface/proc.c:14:2` -- Report the stored active state

1. Write `active=%u\n` to `output` with `seq_printf`.
2. Use `rk_state_is_active(proc_state) ? 1U : 0U` for the value.
3. Return `0`.

Read the value from `proc_state`; do not keep a second copy.

### TODO 2 -- `interface/proc.c:27:2` -- Publish one state object

```text
valid state, no entry  -> create the entry, return 0
missing state          -> return -EINVAL
entry already exists   -> return -EINVAL
creation fails         -> clear proc_state, return -ENOMEM
```

1. Reject `proc_entry != NULL` or `state == NULL` with `-EINVAL`.
2. Store `state` in `proc_state`.
3. Create the entry with the supplied `rk_proc_create` helper.
4. If creation fails, clear `proc_state` and return `-ENOMEM`.
5. Return `0`.

Store `proc_state` before creating the entry so the callback never reads a
`NULL` state.

### TODO 3 -- `interface/proc.c:33:2` -- Remove the proc entry

1. Call `proc_remove(proc_entry)`.
2. Set `proc_entry` to `NULL`.
3. Set `proc_state` to `NULL`.
4. Log `rklings: unpublished proc entry\n`.

Remove the entry first so no new read can use the state.

### TODO 4 -- `core/main.c:32:2` -- Add publication to initialization

1. Call `rk_proc_publish(state)` after state creation.
2. If it returns an error, jump to one cleanup label.
3. After successful publication, log `rklings: loaded\n` and return `0`.
4. At the cleanup label, destroy `state`, clear the global pointer, and return
   the publication error.

Log success only after both the state and proc entry exist.

### TODO 5 -- `core/main.c:39:2` -- Tear down in dependency order

1. Log `rklings: unloaded\n`.
2. Call `rk_proc_unpublish()`.
3. Call `rk_state_destroy(state)`.
4. Set `state` to `NULL`.

Removing the interface first prevents a new reader from entering after its
backing state has been freed.

## Test it

From the repository root, build the module and enter the VM:

```console
$ ./rklings run L03
```

Inside the VM:

```console
root@victim:~# insmod /rklings/rklings.ko active=0
root@victim:~# cat /proc/rklings
active=0
root@victim:~# stat -c '%a' /proc/rklings
444
root@victim:~# rmmod rklings
root@victim:~# cat /proc/rklings
cat: /proc/rklings: No such file or directory
```

## Validate

```console
$ ./rklings validate L03
  [+] Normal procfs lifetime
  [+] State allocation rejection
  [+] Publication rollback
  PASS
```

## Next

Userspace can read live module state but cannot change it.
L04 adds one bounded write callback that accepts `0` or `1` and updates state
safely across the userspace/kernel boundary.
