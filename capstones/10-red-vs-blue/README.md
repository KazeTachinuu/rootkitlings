# Operation Glasshouse

Concealment rarely removes an object from the kernel.

It usually changes one interface so an ordinary tool stops reporting that object.

Operation Glasshouse installs four deliberately imperfect mechanisms, then asks you to find each one through an independent view.

## Deploy

Complete the lifecycle in `main.c`, build it, and load it:

```sh
insmod hello.ko
```

The finished module:

- removes `hello` from the module list;
- removes `.rk` only from proc-root directory results;
- keeps `/proc/.rk` reachable by exact path;
- accepts `escalate` from the calling process; and
- creates `/etc/modules-load.d/glasshouse.conf` with the exact request `hello\n`.

Do not inspect the status entry yet.

Start with the same tools an investigator would have.

## Hunt card

Run these pairs and record where they disagree:

```sh
lsmod | grep hello
test -d /sys/module/hello && echo 'sysfs sees hello'

ls -a /proc | grep '^\.rk$'
stat /proc/.rk

cat /sys/kernel/debug/kprobes/list
cat /etc/modules-load.d/glasshouse.conf
```

Now inspect the direct control path:

```sh
cat /proc/.rk
```

```text
module_hidden=yes
boot_request=present
escalations=0
```

The status is useful context, but it is not independent evidence.

A compromised component can lie through its own interface.

The cross-view disagreements are stronger:

```text
       ordinary view                    independent view

  lsmod: no hello        <-------->  /sys/module/hello exists
  ls /proc: no .rk       <-------->  stat /proc/.rk succeeds
  normal UID             <-------->  same caller becomes UID 0
  runtime-only module    <-------->  boot request exists on disk
```

## Exercise the trigger

Credential replacement affects the process executing the write callback.

Run the supplied client as UID 65534 so the same process writes the command and checks its identity afterward:

```sh
chpst -u 65534:65534 \
  cred_trigger_test /proc/.rk escalate
```

```text
uid=0 euid=0
```

An external `echo` followed by `id` would test two different processes and prove the wrong thing.

## Reveal and remove

A module removed from the module list cannot be found normally by `rmmod`.

Reveal it through the direct control path first:

```sh
echo reveal > /proc/.rk
cat /proc/.rk
lsmod | grep hello
rmmod hello
```

After `reveal`, the live status changes to `module_hidden=no` and `lsmod` sees the module again.

Removal unregisters the probe, removes the proc entry, and deletes the boot request.

## Component boundaries

```text
                         main.c
                acquire / unwind policy
                  /          |          \
                 v           v           v
          +------------+ +---------+ +-------------+
          | control.c  | |conceal.c| |persistence.c|
          | status     | |module   | |owned file   |
          | credentials| |proc view| |transaction  |
          +------------+ +---------+ +-------------+
```

The low-level mechanisms are supplied because they were implemented earlier.

The new work is composing their ownership correctly and then reasoning across trust boundaries.

## Your work

Complete `rootkit_init()` in `main.c`.

Acquire resources in this order:

```text
control -> directory filter -> boot request -> module concealment
```

If the directory filter fails, destroy the control entry.

If boot-request installation fails, stop concealment and then destroy the control entry.

Conceal the module only after every fallible acquisition succeeds.

Complete `rootkit_exit()` in reverse order:

```text
reveal -> remove boot request -> stop filter -> destroy control
```

Module exit cannot return an error.

Emit a warning if boot-request removal fails, then continue releasing kernel resources.

## Verify

```sh
./rootkitlings check C10
```

The check builds an evidence ledger from real views, proves the proc filter does not escape procfs, escalates one unprivileged caller, verifies exact disk bytes, reveals the module, unloads it, and confirms that every observable artifact and probe is gone.

The saved module-list neighbor is acceptable only in this controlled VM with no concurrent module churn.

Direct module-list manipulation is unsupported kernel internals, not a production extension mechanism.
