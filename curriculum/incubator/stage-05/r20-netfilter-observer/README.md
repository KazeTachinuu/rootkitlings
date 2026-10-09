# Observe local IPv4 traffic without changing it

Netfilter calls registered hooks at defined points in the network stack.
This exercise uses the IPv4 `LOCAL_OUT` point, which locally generated IPv4 traffic crosses before routing finishes.
The observer belongs only to `init_net`, so traffic created in another network namespace does not cross this registration.

The hook priority is `NF_IP_PRI_FIRST`.
Hooks run in ascending priority order, so this observer uses the earliest priority value.
That does not define its relative order against another hook registered at the same priority.
An observation here says nothing about what a later hook, route, or receiver will do.

## The observer owns one lifetime

The supplied `struct rk_netfilter_observer` keeps the hook descriptor, callback counter, network namespace, and published registration state together.
Implement its complete lifetime in one file:

- reject a null observer with `-EINVAL`;
- reject an already registered observer with `-EBUSY`;
- reset the counter;
- fully initialize `observer->ops` for `NFPROTO_IPV4`, `NF_INET_LOCAL_OUT`, and `NF_IP_PRI_FIRST`;
- set `ops.priv` to the owning observer;
- register the hook in `init_net` and return the exact registration error;
- set `registered` only after successful registration;
- make stop safe to call for a null or unregistered observer;
- unregister the hook, call `synchronize_rcu()`, and only then clear published state;
- snapshot configured fields with `READ_ONCE()` and the counter with `atomic64_read()`.

Module initialization and exit serialize start and stop.
The component does not promise that concurrent lifecycle calls are safe.

Registration makes the callback reachable from other CPUs.
Unregistration prevents later traversals from selecting this callback, while `synchronize_rcu()` waits for readers that may already hold the old hook entry.
The observer must remain valid until that wait completes.

## The callback

Recover the owning observer from `private`, increment its atomic counter once, and return `NF_ACCEPT`.
`NF_ACCEPT` allows this hook traversal to continue; it does not promise final packet delivery.

Do not read the `sk_buff` yet.
Do not parse, classify, mutate, log, allocate, sleep, queue work, or add a control switch.
The next exercise joins this hook to the packet-access boundary.

The counter measures callback invocations.
One invocation is one traversal of this registered hook, not necessarily one userspace send call, one datagram delivered to an application, or one packet observed on a wire.
The checker expects an exact delta only during a dedicated idle UDP epoch whose traffic it controls.

## Status is not execution evidence

`/proc/rootkitlings` renders the snapshot supplied by your observer as exactly seven lines:

```text
state=registered
family=ipv4
hook=local-out
priority=-2147483648
namespace=init_net
verdict=accept
callbacks=N
```

The family, hook, priority, namespace, and state lines describe the configured object.
They do not prove that the callback ran.
Only a bracketed counter delta provides execution evidence.

## Your task

Edit one file:

- `instrumentation/netfilter_observer.c`

Implement `rk_observe_local_ipv4()`, `rk_netfilter_start()`, `rk_netfilter_stop()`, and `rk_netfilter_snapshot()`.

Run:

```bash
./rootkitlings check
```

The checker builds with `W=1` for the pinned Linux 7.2 kernel.
It verifies exact status output, byte-for-byte IPv4 UDP delivery, controlled counter deltas, IPv6 and child-network-namespace controls, both rollback paths, concurrent reads and traffic, unload during traffic, reload, and a clean kernel log.

## Official Linux v7.2 references

- [Netfilter hook API and `nf_hook_ops`](https://github.com/torvalds/linux/blob/v7.2/include/linux/netfilter.h)
- [IPv4 hook numbers and priorities](https://github.com/torvalds/linux/blob/v7.2/include/uapi/linux/netfilter_ipv4.h)
- [Network-namespace hook registration and unregistration](https://github.com/torvalds/linux/blob/v7.2/net/netfilter/core.c)
- [IPv4 `LOCAL_OUT` hook invocation](https://github.com/torvalds/linux/blob/v7.2/net/ipv4/ip_output.c)
- [Linux RCU synchronization](https://docs.kernel.org/7.2/RCU/whatisRCU.html)
