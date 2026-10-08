# 43 - Create a kernel-owned socket

A Netfilter hook observes packets already crossing the stack.

A kernel socket is an endpoint that later code can use to send or receive traffic without a userspace process owning the descriptor.

This module opens one raw IPv4 ICMP socket and reports its state through `/proc/rk_rawsock`.

## Observe it

On the host, boot the exercise VM:

```sh
./rootkitlings run L43
```

Inside the VM, inspect the socket while the module owns it:

```sh
insmod hello.ko
cat /proc/rk_rawsock
cat /proc/net/raw
rmmod hello
```

```text
socket=open
type=RAW
proto=ICMP
```

The socket also appears in `/proc/net/raw` while the module is loaded.

## Ownership

```text
sock_create_kern(&init_net, AF_INET, SOCK_RAW, IPPROTO_ICMP, &raw_sock)
                                      |
                                      v
                              kernel-owned socket
                                      |
                                      v
                              sock_release(raw_sock)
```

`init_net` selects the initial network namespace.

`sock_create_kern()` marks the socket as kernel-owned, so process attribution is weaker than for a userspace socket.

The network table and packets remain observable.

## Implement it

Replace the marked `-EOPNOTSUPP` in init with one `sock_create_kern()` call using the arguments shown above.

The scaffold already handles the returned error, creates the proc status entry, releases the socket if proc creation fails, and releases it during unload.

## Verify

```sh
./rootkitlings check L43
```

The verification reads the live status, observes the socket in `/proc/net/raw`, unloads the module, and confirms cleanup.

No packet is sent; endpoint creation and ownership are the behavior under test.
