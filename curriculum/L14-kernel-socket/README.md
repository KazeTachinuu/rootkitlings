# Open a kernel TCP socket

The kernel has its own socket API. A module can create a TCP socket, connect to
a remote host, and send data, all without touching userspace. This is how a
rootkit calls home.

## Mechanism

```
sock_create_kern()       Create a kernel socket
  |
  v
kernel_connect()         Connect to remote host:port
  |
  v
kernel_sendmsg()         Send bytes through the socket
  |                       (uses kvec + msghdr)
  v
sock_release()           Clean up
```

The kernel socket API mirrors the userspace flow (socket, connect, send, close)
but uses different types. Data is described by a `struct kvec` (pointer + length)
and metadata by a `struct msghdr`.

## Your task

Edit `main.c`. Complete the init function to:

1. Create a TCP socket with `sock_create_kern`.
2. Fill a `struct sockaddr_in` with the target IP and port.
3. Connect with `kernel_connect`.
4. Send the string `"hello\n"` with `kernel_sendmsg`.
5. Release the socket.

Module parameters `host` and `port` control the target.

## Demo

On the host, start a listener:

```
host$ nc -l 4444
```

In the VM, load the module:

```
vm$ insmod rklings.ko
```

The host terminal prints `hello`.

## Verification

```
./rklings check
```
