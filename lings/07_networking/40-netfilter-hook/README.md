# 40 - Observe packets with Netfilter

Netfilter calls registered hooks as packets cross the IPv4 stack.

This module observes packets at `NF_INET_PRE_ROUTING`, increments a counter, and returns `NF_ACCEPT` so traffic continues unchanged.

## Observe it

On the host, boot the exercise VM:

```sh
./rootkitlings run L40
```

Inside the VM, load the module and generate loopback traffic:

```sh
insmod hello.ko
cat /proc/rk_nfhook
ping -c 3 127.0.0.1
cat /proc/rk_nfhook
```

The first read starts at zero and the second returns a larger number.

## Packet path

```text
incoming IPv4 packet
          |
          v
    PRE_ROUTING hook
          |
     NF_ACCEPT
          |
          v
   routing decision
```

`struct nf_hook_ops` selects the callback, IPv4 family, hook point, and priority.

`nf_register_net_hook(&init_net, &nf_ops)` publishes the hook in the initial network namespace.

`nf_unregister_net_hook()` removes it before module memory is released.

The callback can run concurrently in softirq context and must not sleep.

An `atomic_t` is sufficient for one independent packet counter.

## Implement it

The supplied hook already increments `pkt_count` and returns `NF_ACCEPT`.

In `nfhook_init()`, replace the placeholder error with `nf_register_net_hook(&init_net, &nf_ops)` and keep the returned status in `err`.

In `nfhook_exit()`, call `nf_unregister_net_hook(&init_net, &nf_ops)` after removing the proc entry and before the hook code disappears.

The supplied init rollback already unregisters the hook if proc creation fails.

## Verify

```sh
./rootkitlings check L40
```

The verification reads the live counter, sends real packets through loopback, requires the counter to increase, and confirms proc cleanup after unload.

The counter proves that the callback ran.

Packet capture and Netfilter inspection remain independent evidence of the traffic and hook.
