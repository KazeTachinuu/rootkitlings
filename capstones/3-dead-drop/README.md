# Capstone 3: Dead drop

Build a hidden message board in the kernel. Two operatives communicate
through a channel that nobody else can see. Not even the sysadmin.

```
  handler                       kernel module                     asset
  +----------+                 +---------------------+          +----------+
  |          | kill -63 $$     |                     |          |          |
  |  reveal  |---------------->| signal handler      |          |          |
  |          |                 |   show /proc/.drop  |          |          |
  +----------+                 +---------------------+          +----------+
       |                              |                              |
       v                              v                              v
  echo "msg" > /proc/.drop     +---------------------+     cat /proc/.drop
       |                       |  struct dead_drop    |          |
       +---------------------->|    char msg[256]     |--------->+
                               |    bool visible      |
                               +---------------------+
                                      |
  kill -64 $$                         v
  +----------+                 +---------------------+
  |          |---------------->| hide /proc/.drop    |
  |  hide    |                 | escalate to root    |
  +----------+                 +---------------------+

  blue team view:
  $ ls /proc/ | grep drop        (nothing)
  $ find /proc -name '.drop'     (nothing)
  $ cat /proc/.drop              (works if you guess the name)
```

## Prerequisites

Lings 00 through 40 (foundation through hiding).

## What you are building

Kernel-level spy tradecraft. A dead drop that exists in memory, is
invisible to directory listings, but accessible by direct path.

- `/proc/.drop` is created at init but hidden from `getdents64`.
- `kill -63` on your own process reveals the interface (sets a flag).
- Write a message: `echo "intel" > /proc/.drop`.
- Read a message: `cat /proc/.drop`.
- `kill -64` hides it again and escalates the caller to root.
- Messages live in a heap buffer. Gone on reboot.

## What "done" looks like

```sh
insmod hello.ko
ls /proc/ | grep drop          # nothing
cat /proc/.drop                # permission denied (hidden state)
kill -63 $$
echo "asset compromised, exfil at 0300" > /proc/.drop
cat /proc/.drop                # "asset compromised, exfil at 0300"
kill -64 $$
id                             # uid=0(root)
ls /proc/ | grep drop          # nothing again
rmmod hello
```

## The blue team angle

Give your module to a classmate. Tell them there is a hidden message
somewhere in /proc. Buy them a coffee from the sm14 machine if they
find it. Which layers hold? Which ones leak?

Detection vectors (easiest to hardest):
- `dmesg` shows the module load (unless you added log scrubbing)
- `cat /proc/sys/kernel/tainted` changed from 0 to 12288+ (out-of-tree module loaded)
- `cat /proc/.drop` works if you guess the name (direct path bypasses getdents64)
- `stat /proc/.drop` reveals metadata even when hidden from listings
- Cross-view diff: count entries in `/proc` via getdents64 vs actual inodes
- `cat /sys/kernel/tracing/enabled_functions` shows the getdents64 hook
- `cat /proc/kallsyms | grep hello` may expose symbols you forgot to scrub

The fundamental problem: you hid from one view (directory listing) but
not from direct access, tracing, or memory inspection. Each layer you
add in later lings closes one more gap. Capstone 5 tests them all.

## Contract

The check script verifies:

- `.drop` is hidden from `ls /proc/` (not in directory listing).
- Reading `/proc/.drop` while hidden is denied.
- `kill -63 $$` reveals the interface (read and write work).
- Message round-trips correctly (write then read returns same string).
- `kill -64 $$` hides it again and escalates to root (`id -u` returns 0).
- After hide, reads are denied again and listing still shows nothing.

## Verify

```bash
./rootkitlings check capstone-3
```

## Hints

