# L24: Anti-Forensics

## Mission

L23 changes the process name and prevents core dumps, but deployment still
leaves traces:

- Timestamps
- Login records
- Shell history
- System journal

This exercise identifies and modifies those specific local records.

## What you will observe

You will issue one cleanup command, then inspect each affected record directly:

```console
root@victim:~# last | grep student
root@victim:~# wc -c < /home/student/.bash_history
0
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

## Traces to scrub

Each of these traces leaves evidence that a different tool exposes:

- File timestamps
- Shell history
- utmp/wtmp login records
- The systemd journal
- The process environment

The sections below cover each one.

## Timestomping

`stat` shows three timestamps:

- atime (last read)
- mtime (last write)
- ctime (last metadata change)

An attacker who drops a file at 3 AM on a server with no nighttime activity
stands out.
`os.Chtimes` sets atime and mtime to any value you choose.

The standard technique: copy the timestamp of a nearby legitimate file, so
your file blends into the directory listing sorted by time.

```text
  before:
    -rw-r--r-- 1 root root 0 Oct 17 03:14 agent.bin   <-- stands out
    -rw-r--r-- 1 root root 0 Jan 15 09:22 resolv.conf

  after timestomp (copy resolv.conf's mtime):
    -rw-r--r-- 1 root root 0 Jan 15 09:22 agent.bin   <-- blends in
    -rw-r--r-- 1 root root 0 Jan 15 09:22 resolv.conf
```

ctime cannot be set from userspace, so forensic tools that compare ctime to
mtime can still detect the change.

## utmp/wtmp scrubbing

`last` and `who` read binary records from `/var/run/utmp` (current logins)
and `/var/log/wtmp` (login history).
Each record is a 384-byte `utmp` struct in the course's amd64 guest.

To scrub:

1. Read the file
2. Walk records
3. Zero out entries matching your username or terminal
4. Write back

The record layout (from utmpx.h):

```text
  offset  size  field
  0       2     ut_type (7 = USER_PROCESS)
  4       4     ut_pid
  8       32    ut_line ("pts/0")
  40      4     ut_id
  44      32    ut_user ("student")
  76      256   ut_host
  ...
  384     next record
```

Walk the file in 384-byte chunks.
If ut_user matches your username, zero the entire record.
Write the modified buffer back.

## Journal tampering

systemd-journal records everything: logins, sudo, SSH, service starts.
`journalctl --since "1 hour ago"` shows your activity.

Three journalctl flags handle the cleanup:

- `--sync` flushes pending log data to disk so nothing is left in memory.
- `--rotate` closes the active journal file and starts a fresh one.
- `--vacuum-time=1s` deletes archived journal files older than one second.

Run `--sync` first, then `--rotate --vacuum-time=1s` together.
The rotate moves the current entries into an archive, and the vacuum immediately
removes that archive.

## TODOs

```text
TODO 1   timestomp     stat the reference file, apply its times to target
TODO 2   clearHistory  truncate history files
TODO 3   scrubUtmp     zero matching utmp and wtmp records in place
TODO 4   tamperJournal run journalctl --sync
TODO 5   tamperJournal run journalctl --rotate --vacuum-time=1s
TODO 6   cleanup       call timestomp on the agent binary
TODO 7   cleanup       call clearHistory
TODO 8   cleanup       call scrubUtmp
TODO 9   cleanup       call tamperJournal
TODO 10  cleanup       call os.Clearenv
```

### TODO 1 -- Timestomp

Stat the reference file with `os.Stat` and copy its modification time to the
target with `os.Chtimes`.

### TODO 2 -- Clear history

Iterate over the `paths` slice and truncate each file with `os.Truncate`.
Add `/home/<username>/.bash_history` if username is not empty.
Ignore errors from files that do not exist.

### TODO 3 -- Scrub utmp and wtmp

1. Open each binary record file with `os.O_RDWR`
2. Read `utmpRecordSize`-byte records with `io.ReadFull`
3. When `extractUser(record)` matches the username, zero the buffer with
   `clear`, seek back one record, and overwrite it

### TODO 4 -- Sync journal

Run `journalctl --sync` with `exec.Command` to flush pending entries.

### TODO 5 -- Vacuum journal

Run `journalctl --rotate --vacuum-time=1s` to rotate and purge the journal.

### TODOs 6-10 -- Orchestrate cleanup

Call each routine in order: timestomp the agent binary using `/etc/hostname` as
the time reference, clear history, scrub utmp, tamper the journal, and wipe the
process environment with `os.Clearenv`.

## Test it

```console
$ ./rklings run L24
```

The agent connects and waits for commands.
After sending a `CLEANUP student` command through the server, inspect the guest:

```text
root@victim:~# last | grep student
(no output)
root@victim:~# cat /home/student/.bash_history
(empty)
root@victim:~# journalctl --since "1 hour ago" 2>&1 | head -1
-- No entries --
```

An empty `last` result means the utmp/wtmp records were scrubbed.
An empty history file and a vacuumed journal confirm the other routines.

## Validate

```console
$ ./rklings validate L24
  [+] file timestamps match reference time
  [+] shell history scrubbed
  [+] no login record in utmp/wtmp
  [+] environment variables cleared
  [+] journal entries suppressed
  PASS
```

## Next

The cleanup controls work separately.
M03 runs them as one ordered deployment and checks that the agent remains
controllable afterward.
