# Complete remote management with auto-OPSEC

This milestone verifies that every capability built across M00 through L26 works
together as one integrated system. Every command is available remotely over the
encrypted channel. The agent self-hides on connect. The operator has full parity
with the local mkdir control interface.

## What you have built

By this point, the rootkit contains:

- 5 kretprobes (mkdir, getdents64, read, tcp4_seq_show, tcp6_seq_show)
- Agent kthread with encrypted framing (AES-256-GCM or equivalent)
- Full protocol parser and command dispatcher
- All target lists (files, PIDs, content strings, ports)
- Module cloaking (list_del from module list)
- Boot persistence (modules-load.d or equivalent)
- Privilege escalation (prepare_kernel_cred + commit_creds)
- Shell and exec slots with timeouts
- File download (chunked kernel_read)
- Key management with memzero_explicit
- Auto-OPSEC on connect/disconnect

## Integration test

The test for this milestone exercises the full cycle:

```text
1. Generate a 256-bit key
2. Load the module with the key as a parameter
3. Verify: module loads silently (nothing in dmesg)
4. Connect from an operator client with the matching key
5. Send PING, expect PONG
6. Send INFO, verify hostname and kernel version
7. Send STATUS, verify initial state (all counts zero)
8. HIDE FILE secret.txt, verify ls no longer shows it
9. HIDE PID <pid>, verify ps no longer shows it
10. HIDE PORT 8080, verify ss no longer shows it
11. LIST FILE, LIST PID, LIST PORT: verify entries
12. PERSIST, verify /etc/modules-load.d/rklings.conf exists
13. UNPERSIST, verify it is removed
14. ESCALATE, verify uid is now 0
15. DOWNLOAD /etc/hostname, verify contents match
16. Disconnect, verify auto-OPSEC entries are removed
17. Unload module, verify clean shutdown
```

## Architecture

```text
                  operator
                     |
                 encrypted TCP
                     |
          +----------+----------+
          |    agent kthread    |
          |  (auto-hide on     |
          |   connect)         |
          +----+----+----+-----+
               |    |    |
     +---------+    |    +----------+
     |              |               |
  protocol      target lists     kretprobes
  dispatcher    (file,pid,       (mkdir, getdents64,
  (all cmds)     content,port)    read, tcp4/6_seq)
     |              |               |
  handlers:      add/remove      intercept
  INFO,STATUS    hide/unhide     syscalls
  LIST,HIDE
  PERSIST
  ESCALATE
  DOWNLOAD
  SHELL,EXEC
```

## Your task

Combine all subsystems into one module. Verify that:

1. The module loads with no dmesg output (OPSEC).
2. Every command works over the encrypted channel.
3. Auto-OPSEC hides the agent on connect, unhides on disconnect.
4. The init function has a complete goto rollback chain.
5. The exit function tears down every resource in reverse order.
6. All sensitive memory is zeroed with memzero_explicit.

Run:

```sh
./rklings check
```

## References

- All references from L00 through L26 apply here
- [`kthread_run`](https://github.com/torvalds/linux/blob/v7.2/include/linux/kthread.h)
- [`crypto_alloc_aead`](https://github.com/torvalds/linux/blob/v7.2/include/crypto/aead.h)
