# Networking

Open kernel sockets, build a C2 channel, and make it resilient.

Each ling teaches one networking concept. Combine what your design needs.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 41 | Kernel socket | `sock_create`, `kernel_connect`, `kernel_sendmsg`. | Connection is plaintext. Visible in `ss` and `tcpdump`. |
| 42 | Stream I/O | Handle partial send/recv, `EINTR`, EOF. | Naive single-recv drops data. Partial reads corrupt framing. |
| 43 | Framing | Length-prefix each message for bounded parsing. | Fixed framing is fingerprintable. Protocol looks unlike normal traffic. |
| 44 | Netfilter hook | `nf_register_net_hook`, inspect/drop/accept packets. | Magic packet trigger (Reptile/Syslogk pattern). Static magic bytes are signature-matchable. |
| 45 | Beacon naive | Kthread sends checkin with system metadata, pulls tasks. | **Periodic 60s pattern visible in netflow.** IDS flags regular intervals. |
| 46 | Beacon jitter | `get_random_u32() % jitter` added to sleep interval. | Timing is now irregular. But volume and destination are still constant. |
| 47 | Task queue | `list_head` task queue with mutex, dequeue/execute/report. | Core C2 loop. Without a task queue, the beacon can only check in, not receive work. |
| 48 | Reconnect | Bounded exponential backoff with jitter on failure. | Without cap, backoff grows to hours. Without jitter, retries synchronize. |
| 49 | Covert channel | ICMP-based C2 via netfilter, magic value in ID field. | No listening port. ICMP payload entropy analysis detects non-standard payloads. |

Ling 43 is the key teaching moment: run the naive beacon, capture with `tcpdump`,
plot the timestamps, see the perfect 60-second intervals. Ling 44 shows how jitter
makes the pattern disappear. Your rootkit works fine without jitter, but a defender
watching netflow spots the regular interval immediately.

Ling 45 (task queue) uses `list_head` (see ling 11 if unfamiliar) with a mutex
(ling 19). It is the data structure behind every C2 framework: AdaptixC2, Sliver,
Cobalt Strike all use the same pull-based pattern.

Ling 47 (covert channel) teaches the Reptile/BPFDoor/VoidLink pattern: embed
commands in ICMP echo payloads. No TCP socket, no listening port, invisible to `ss`.

After ling 49: build [Capstone 4: Remote shell](../../capstones/4-remote-shell/README.md).
