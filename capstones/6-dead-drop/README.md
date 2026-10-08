# Sealed dead drop

A dead drop lets two operators exchange a message without keeping a session open.

This one stores a single message in kernel memory at `/proc/.drop`.

The entry is absent from `/proc` listings, and direct reads and writes fail while the drop is locked.

## Play with it

Load the module and compare two views:

```sh
insmod hello.ko
ls -a /proc | grep '^\.drop$'
stat /proc/.drop
cat /proc/.drop
```

The listing finds nothing, `stat` finds the entry by its exact path, and `cat` receives `EACCES` because the drop is locked.

Knock on the unlock path, leave a message, and lock it again:

```sh
mkdir /tmp/.rk-unlock
rmdir /tmp/.rk-unlock
echo 'package moved to platform 4' > /proc/.drop
cat /proc/.drop
mkdir /tmp/.rk-lock
rmdir /tmp/.rk-lock
cat /proc/.drop
```

The knock directories are real and the original syscall removes them normally.

The probe observes each removal and changes the gate before the syscall returns.

## Three independent paths

```text
  ls -a /proc              rmdir knock              direct access
       |                       |                          |
       v                       v                          v
+---------------+      +----------------+      +-------------------+
| getdents64    |      | rmdir kprobe   |      | /proc/.drop      |
| remove .drop  |      | lock / unlock  |      | read or write    |
+---------------+      +----------------+      +---------+---------+
                                                       |
                                                locked | unlocked
                                               EACCES  | message
```

Hiding changes only directory enumeration.

It does not remove the proc entry, so direct lookup remains a reliable detection path.

The rmdir callback runs in probe context and must not sleep.

An atomic gate publishes the locked state; a mutex protects the message in normal proc read and write callbacks.

## Files

```text
main.c       acquire components and unwind in reverse order
drop.c       access gate, message, and proc interface
dirhide.c    validated proc-root directory filtering
trigger.c    bounded path copy and rmdir knock policy
dead_drop.h  names and component contracts
```

The complete directory-record pipeline is provided because it was implemented in the previous project.

Here the work is the integration policy.

## Your work

In `trigger.c`, make `handle_knock()` unlock only for `UNLOCK_PATH` and lock only for `LOCK_PATH`.

Unknown rmdir paths must not change the state.

Do not take the message mutex from the probe callback.

## Verify

```sh
./rootkitlings check C06
```

The check uses a message created at runtime, tests locked reads and writes, rejects an oversized write without losing the previous message, proves that a `.drop` file outside procfs remains visible, and verifies probe cleanup after unload.
