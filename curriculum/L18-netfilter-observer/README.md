# Place a passive watch on outbound IPv4

Install a traffic watch where locally generated IPv4 leaves the host network
stack.
Count each visit and always allow the packet to continue unchanged.

Success proves that you can register, publish, drain, and remove a Netfilter
hook safely.
Netfilter is Linux's framework for running callbacks at packet-processing
points.

Place the watch at `LOCAL_OUT` in `init_net` only.

Use `NF_IP_PRI_FIRST`, the earliest numeric priority.
Equal-priority order is unspecified, and later processing may still change or
drop the packet.

## The observer owns one lifetime

The supplied `struct rk_netfilter_observer` keeps the hook descriptor, callback
counter, network namespace, and published registration state together.
Implement its complete lifetime in one file:

- reject a null observer with `-EINVAL`;
- reject an already registered observer with `-EBUSY`;
- reset the counter;
- fully initialize `observer->ops` for `NFPROTO_IPV4`, `NF_INET_LOCAL_OUT`, and
  `NF_IP_PRI_FIRST`;
- set `ops.priv` to the owning observer;
- register the hook in `init_net` and return the exact registration error;
- set `registered` only after successful registration;
- make stop safe to call for a null or unregistered observer;
- unregister the hook, call `synchronize_rcu()`, and only then clear published
  state;
- snapshot configured fields with `READ_ONCE()` and the counter with
  `atomic64_read()`.

Module init and exit serialize start and stop.

Unregister blocks new selection; `synchronize_rcu()` drains readers already
holding the hook.
Keep the observer valid until it completes.

The callback runs inside a Netfilter RCU read-side critical section.
It must not sleep.

## The callback

Recover the owning observer from `private`, increment its atomic counter once,
and return `NF_ACCEPT`.
`NF_ACCEPT` allows this hook traversal to continue; it does not promise final
packet delivery.

Do not read the `sk_buff` yet.
Do not parse, classify, mutate, log, allocate, sleep, queue work, or add a
control switch.
The next exercise joins this hook to the packet-access boundary.

The counter measures hook traversals, not sends, deliveries, or wire packets.

## Status is not execution evidence

`/proc/rootkitlings` renders the snapshot supplied by your observer as exactly
seven lines:

```text
state=registered
family=ipv4
hook=local-out
priority=-2147483648
namespace=init_net
verdict=accept
callbacks=N
```

Configuration fields do not prove execution; compare the counter around
controlled traffic.

## Your task

Edit one file:

- `instrumentation/netfilter_observer.c`

Implement `rk_observe_local_ipv4()`, `rk_netfilter_start()`,
`rk_netfilter_stop()`, and `rk_netfilter_snapshot()`.

Run:

```bash
./rootkitlings check
```

The checker builds with `W=1` and verifies exact status, delivery, counter
deltas, namespace controls, rollback, concurrent traffic, unload, and reload.

## Official Linux v7.2 references

- [Netfilter hook API and `nf_hook_ops`][1]
- [IPv4 hook numbers and priorities][2]
- [Network-namespace hook registration and unregistration][3]
- [IPv4 `LOCAL_OUT` hook invocation][4]
- [Linux RCU synchronization][5]

[1]: https://github.com/torvalds/linux/blob/v7.2/include/linux/netfilter.h
[2]:
  https://github.com/torvalds/linux/blob/v7.2/include/uapi/linux/netfilter_ipv4.h
[3]: https://github.com/torvalds/linux/blob/v7.2/net/netfilter/core.c
[4]: https://github.com/torvalds/linux/blob/v7.2/net/ipv4/ip_output.c
[5]: https://docs.kernel.org/7.2/RCU/whatisRCU.html
