# Capstone 5: Red vs blue

Build three concealment mechanisms, then use their leaks to find them.

## Red: build it

Combine mechanisms already implemented in earlier lings:

1. Hide the module from `/proc/modules` and `/sys/module`.
2. Create `/proc/.rk` and filter its name from directory listings.
3. Keep `/proc/.rk` reachable by direct path.
4. Accept `escalate` through `/proc/.rk` and escalate the writing process.
5. Write `/etc/modules-load.d/hello.conf` for boot persistence.
6. Release probes and proc state on module exit.

`/proc/.rk` reports the active hiding, persistence, and escalation paths. It is
a status interface for the exercise, not a command channel.

## Blue: find it

Ask the same question through independent views. A disagreement is evidence.

| Hidden view | Independent check | Expected leak |
|-------------|-------------------|---------------|
| `lsmod` | `/sys/module`, kernel memory | one view may still retain module state |
| `ls /proc` | `stat /proc/.rk` | direct lookup bypasses directory filtering |
| hidden trigger | inspect proc metadata and module memory | writable callback remains |
| normal boot config | inspect `/etc/modules-load.d` | `hello.conf` is a disk artifact |
| ordinary user | trigger, then run `id -u` in the same process | unexplained UID transition |

Run the basic cross-view checks inside the guest:

```sh
lsmod | grep hello
test -d /sys/module/hello && echo visible
ls /proc | grep '^\.rk$'
stat /proc/.rk
cat /sys/kernel/debug/kprobes/list
cat /etc/modules-load.d/hello.conf
```

The guest can interfere with guest tools. A host-side memory dump or packet
capture sits outside that trust boundary and is therefore stronger evidence.

## Contract

The automated check verifies module and proc-list hiding, direct-path leakage,
escalation of a process that starts as UID 65534, the persistence file, and the
`stat` cross-view detection.

```bash
./rootkitlings check capstone-5
```

There is no hint. Revisit the earlier lings named by the failed assertion.
