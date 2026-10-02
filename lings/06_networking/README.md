# Networking

Open kernel sockets, build a C2 channel, and make it resilient.

Each ling teaches one networking concept. Combine what your design needs.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 43 | Netfilter hook | `nf_register_net_hook`, inspect/drop/accept packets. | Magic packet trigger (Reptile/Syslogk pattern). Static magic bytes are signature-matchable. |
| 44 | Packet sniffer | Capture and log packets matching a filter from kernel space. | All captured data visible in dmesg. No encryption, no filtering by source. |
| 45 | ICMP trigger | Detect a magic ICMP echo request and activate a feature. | No listening port. ICMP payload entropy analysis detects non-standard payloads. |
| 46 | Raw socket | `sock_create`, `kernel_sendmsg`, `kernel_recvmsg` for raw comms. | Connection is plaintext. Visible in `ss` and `tcpdump`. |
| 47 | DNS spoof | Hook DNS responses to redirect lookups. | Defenders compare DNS results across resolvers. DNSSEC breaks spoofing. |
| 48 | TCP backdoor | Kernel TCP socket connects back to operator, sends/receives data. | Listening or connecting socket visible in `ss`, `netstat`, `/proc/net/tcp`. |
| 49 | Covert channel | ICMP-based C2 via netfilter, magic value in ID field. | No TCP socket, no listening port, invisible to `ss`. ICMP payload entropy analysis detects it. |
| 50 | Traffic counter | Count packets/bytes per protocol via netfilter hooks. | Useful for reconnaissance. The hook itself is visible in tracing. |
| 51 | Packet mangling | Modify packet contents in-flight via netfilter. | Modified checksums and payload patterns are detectable. |

Ling 45 (ICMP trigger) teaches the Reptile/BPFDoor/VoidLink pattern: embed
commands in ICMP echo payloads. No TCP socket, no listening port, invisible to `ss`.

Ling 48 (TCP backdoor) is the core C2 transport. Combined with ling 16
(call_usermodehelper), it becomes a remote shell.

After ling 51: build [Capstone 4: Implant](../../capstones/4-implant/README.md).
