# Capstone 1: Command box

Build a module that accepts commands through proc and ioctl, executes them
via `call_usermodehelper`, and shows the output in `/proc/rk_cmdbox`.

```
  userspace                              kernel module
  +-------------------+                  +-----------------------------+
  |                   |  write to proc   |                             |
  |  echo "add:id"    |----------------->|  proc_write()               |
  |   > /proc/rk_cmdbox   |                  |    |                        |
  +-------------------+                  |    v                        |
                                         |  +-----------------------+  |
  +-------------------+                  |  |  struct cmd_entry     |  |
  |                   |  read proc       |  |    char text[64]      |  |
  |  cat /proc/rk_cmdbox  |<----------------|  |    char output[256]   |->...
  |                   |                  |  +-----------------------+  |
  +-------------------+                  +-----------------------------+

  echo "add:id"    > /proc/rk_cmdbox  -> queues command
  echo "add:whoami" > /proc/rk_cmdbox -> queues another
  echo "exec"      > /proc/rk_cmdbox  -> runs all, captures output
  echo "clear"     > /proc/rk_cmdbox  -> frees all entries
  cat /proc/rk_cmdbox                 -> shows command + output pairs
```

## Prerequisites

Lings 00 through 15 (foundation + interfaces).

## Structure

This is your first module that outgrows a single file. Split it:

```
  module/
  +-- Kbuild          hello-y := main.o cmds.o
  +-- main.c          init/exit, proc setup, miscdevice
  +-- cmds.c          command list, add/exec/clear logic
  +-- cmds.h          struct cmd_entry, function prototypes
```

See "Multi-file modules" in the main README for the Kbuild syntax.

## What you are building

A control surface with no network. This is the shape of every C2 implant
before the socket gets wired in.

- A miscdevice at `/dev/rk_cmdbox` accepts ioctl commands (CMD_ADD, CMD_CLEAR, CMD_EXEC).
- `/proc/rk_cmdbox` accepts writes: `add:CMD`, `exec`, `clear`.
- `exec` runs each queued command via `call_usermodehelper`, captures stdout
  to a temp file, reads it back with `kernel_read`.
- `cat /proc/rk_cmdbox` prints each command and its captured output.
- Exit clears the list, removes procfs, and deregisters the device.

## What "done" looks like

```sh
insmod hello.ko
echo "add:id" > /proc/rk_cmdbox
echo "add:uname -r" > /proc/rk_cmdbox
cat /proc/rk_cmdbox
# [pending] id
# [pending] uname -r
echo "exec" > /proc/rk_cmdbox
cat /proc/rk_cmdbox
# $ id
# uid=0(root) gid=0(root)
# $ uname -r
# 7.2.5-4-omarchy
echo "clear" > /proc/rk_cmdbox
rmmod hello
```

## What a defender sees

`/dev/rk_cmdbox` appears in the device list. `/proc/rk_cmdbox` is world-readable.
`strace` shows the ioctl numbers in plain sight. No obfuscation yet.

## Contract

The check script verifies:

- `/proc/rk_cmdbox` and `/dev/rk_cmdbox` exist after loading.
- Queue is empty on start.
- `add:COMMAND` queues a command (shown as `[pending]`).
- `exec` runs all queued commands (no more `[pending]`, output captured).
- Command output is accurate (`id` shows `uid=0`, `uname -r` matches).
- `clear` empties the queue.
- Both entries disappear after unload.

## Verify

```bash
./rootkitlings check capstone-1
```

## Hints

