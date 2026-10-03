# Capstone 3: Dead drop

Build a dead drop in kernel memory. An operator reveals it, leaves a message,
and hides it again. A defender tests which views still expose the channel.

```text
  operator                       kernel module                    asset
+-----------+                 +-------------------+            +-----------+
|           |  rmdir reveal   | rmdir hook        |            |           |
| controller+---------------->| visible = true    |            | receiver  |
|           |                 +---------+---------+            |           |
+-----------+                           |                      +-----+-----+
       |                                v                            ^
       | write                +-------------------+       read       |
       +--------------------->| /proc/.drop       |------------------+
                              | message[256]      |
                              +---------+---------+
                                        ^
                                        |
                              rmdir hide|  visible = false
                                        |
                              +---------+---------+
                              | directory filter  |
                              | hides ".drop"     |
                              +-------------------+

  listing view:  ls /proc       -> no .drop
  direct view:   stat /proc/.drop -> entry still exists
```

## Prerequisites

Lings 00 through 42 (foundation through hiding).

## What you are building

Kernel-level spy tradecraft. A dead drop that exists in memory, is
invisible to directory listings, but accessible by direct path.
Controlled via rmdir, the Pumakit pattern (ling 34).

- `/proc/.drop` is created at init but hidden from `getdents64`.
- `rmdir /tmp/.rk-reveal` reveals the interface (sets a flag).
- Write a message: `echo "intel" > /proc/.drop`.
- Read a message: `cat /proc/.drop`.
- `rmdir /tmp/.rk-hide` hides it again.
- Messages live in a heap buffer. Gone on reboot.
- Both rmdir commands return success but never remove anything.

## What "done" looks like

```sh
insmod hello.ko
ls /proc/ | grep drop          # nothing
cat /proc/.drop                # permission denied (hidden state)
rmdir /tmp/.rk-reveal
echo "asset compromised, exfil at 0300" > /proc/.drop
cat /proc/.drop                # "asset compromised, exfil at 0300"
rmdir /tmp/.rk-hide
ls /proc/ | grep drop          # nothing again
rmmod hello
```

## The blue team angle

Give your module to a classmate. Tell them there is a hidden message
somewhere in /proc. Which layers hold? Which ones leak?

Detection vectors (easiest to hardest):
- `dmesg` shows the module load (unless you added log scrubbing)
- `cat /proc/sys/kernel/tainted` changed from 0 to 12288+ (out-of-tree module loaded)
- `cat /proc/.drop` works if you guess the name (direct path bypasses getdents64)
- `stat /proc/.drop` reveals metadata even when hidden from listings
- Cross-view diff: count entries in `/proc` via getdents64 vs actual inodes
- `cat /sys/kernel/tracing/enabled_functions` shows the getdents64 and rmdir hooks
- `cat /proc/kallsyms | grep hello` may expose symbols you forgot to scrub

The fundamental problem: you hid from one view (directory listing) but
not from direct access, tracing, or memory inspection. Each layer you
add in later lings closes one more gap. Capstone 5 tests them all.

## Contract

The check script verifies:

- `.drop` is hidden from `ls /proc/` (not in directory listing).
- Reading `/proc/.drop` while hidden is denied.
- `rmdir /tmp/.rk-reveal` reveals the interface (read and write work).
- Message round-trips correctly (write then read returns same string).
- `rmdir /tmp/.rk-hide` hides it again.
- After hide, reads are denied again and listing still shows nothing.
- Real rmdir still works (create and remove a normal directory).

## Verify

```bash
./rootkitlings check capstone-3
```

## Hints

Only if stuck. Try without them first.
