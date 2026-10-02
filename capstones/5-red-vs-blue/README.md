# Capstone 5: Red vs blue

The final exam is a game against yourself. Install a rootkit, then
switch hats and try to break every hiding layer.

```
  PHASE 1: RED                            PHASE 2: BLUE
  +--------------------------+            +--------------------------+
  |                          |            |                          |
  |  Build and install:      |            |  Detect and document:    |
  |    - hidden module       |  reboot    |    - cross-view diff     |
  |    - hidden proc entry   |  the VM    |    - ftrace inspection   |
  |    - credential backdoor |----------->|    - memory forensics    |
  |    - encrypted C2        |            |    - network capture     |
  |    - boot persistence    |            |    - integrity checks    |
  |    - log scrubbing       |            |    - hypervisor dump     |
  |                          |            |                          |
  |  Verify: check script    |            |  Score each layer        |
  +--------------------------+            +--------------------------+
```

## Prerequisites

All lings (00 through 57).

## What you are building

A full rootkit assembled from every concept in the course. Then you
reboot, put on the blue hat, and systematically attack every hiding layer.

Your rootkit must combine:
- Module hiding (ling 34) and sysfs cleanup (ling 35)
- Procfs entry hidden from getdents64 (ling 36)
- Credential escalation via signal (ling 25)
- Encrypted C2 over TCP (lings 43-46 + 53-55)
- Boot persistence (ling 52)
- Log scrubbing (ling 38)

## Phase 1: Red hat

Build and install your rootkit. Verify every layer:

```sh
./rootkitlings check capstone-5
```

## Phase 2: Blue hat

Reboot the VM. Pretend you have never seen the rootkit. Work through
each detection level below. For every check, write down: did it
catch anything, and what exactly did it reveal?

---

## Detection playbook

### Level 1: Basic enumeration

The checks every admin runs first. Your rootkit should survive all of these.

```
[ ] lsmod                                   module in the list?
[ ] ls /sys/module/                          sysfs directory present?
[ ] ls /proc/ | grep hello                   entry visible?
[ ] ps aux                                   suspicious kthreads?
[ ] ss -tlnp                                 unexpected listeners?
[ ] dmesg | grep hello                       load traces?
[ ] cat /proc/sys/kernel/tainted             taint flags changed?
```

Value 0 = clean kernel. Loading an out-of-tree module sets bits 12+13
(value 12288+). Your rootkit cannot prevent this without hooking the
sysctl read.

### Level 2: Cross-view diffing

Ask the same question through two paths. Disagreement = detection.

```
[ ] cat /proc/modules                        vs lsmod
[ ] ls /sys/module/                          vs /proc/modules
[ ] cat /proc/kallsyms | grep hello          symbols still exposed?
[ ] for i in $(seq 1 65535); do              brute-force /proc PIDs
      [ -d /proc/$i ] && echo $i             vs ps output
    done
[ ] cat /proc/net/tcp                        vs ss output
```

A rootkit must hook ALL views consistently. If it hides from `lsmod`
(linked list) but forgets `/sys/module/` (kobject tree), cross-view
catches it.

### Level 3: Ftrace and hook inspection

The kernel exposes its own hook state. Check it.

```
[ ] cat /sys/kernel/tracing/enabled_functions
```

If `do_filp_open`, `__x64_sys_getdents64`, or `__x64_sys_kill` appears
here, something is hooking it. This is the most direct way to find
ftrace-based rootkits.

```
[ ] cat /sys/kernel/tracing/touched_functions    (kernel 6.4+)
```

`touched_functions` is a bitmap of every function that was EVER hooked,
even if the hook was later removed. Harder to fake.

Evasion: Singularity intercepts reads to `ftrace_enabled` sysctl and
returns fake values. Counter: read tracefs from a detection kernel module
that bypasses the sysctl.

### Level 4: Syscall table integrity

Read `sys_call_table` entries from `/proc/kallsyms`. Every address should
fall within kernel core text. An address in module memory = hook.

```
[ ] cat /proc/kallsyms | grep sys_call_table
[ ] cat /proc/kallsyms | grep ' T '          core_text boundaries
```

Note: ftrace-based hooks do NOT modify the syscall table. They patch the
function prologue. A clean syscall table does not mean clean syscalls.

### Level 5: Direct path probing

`getdents64` filtering hides entries from directory listings. But
`open()` resolves paths directly to inodes. If you guess the name,
you find it.

```
[ ] cat /proc/.drop                          common hidden names
[ ] stat /proc/.drop                         metadata leaks
[ ] cat /proc/.hello
[ ] cat /proc/.cmd
```

Wordlist approach: try common rootkit names. Brute-force approach:
try every printable name up to 8 characters (slow but complete).

### Level 6: Network forensics

Capture traffic on the HOST side, outside the VM. The rootkit cannot
touch packets after they leave the guest.

```
[ ] tcpdump -i eth0 -w capture.pcap          inside VM (can be hooked)
[ ] tcpdump on HOST bridge/tap interface      outside VM (unhooked)
```

Check the capture: is the C2 traffic encrypted or plaintext?
Can you identify the beacon interval from packet timing?

Host-side capture is the definitive network detection. No kernel rootkit
can hide from it.

### Level 7: Memory forensics

Dump VM memory from the hypervisor. The guest kernel cannot intercept this.

```
# From the QEMU monitor (Ctrl-A C):
(qemu) dump-guest-memory /tmp/memdump.raw

# Or with virsh:
virsh dump <domain> /tmp/memdump.raw --memory-only
```

Analyze with Volatility 3:
```
[ ] linux.check_modules                      finds hidden modules
[ ] linux.check_syscall                      finds hooked syscalls
[ ] linux.hidden_modules                     scans for struct module patterns
[ ] linux.check_creds                        finds uid=0 escalation
```

This finds modules that unlinked themselves from the list by scanning
raw memory for `struct module` patterns: valid name field, valid state
enum, valid init/exit pointers.

Evasion: zero the `struct module` fields after init (lose the ability to
cleanly unload). Counter: scan for executable memory pages in the module
address range that have no owner.

### Level 8: Boot persistence artifacts

```
[ ] diff /etc/modules-load.d/ vs clean snapshot
[ ] diff /etc/modprobe.d/ vs clean snapshot
[ ] diff /etc/rc.local vs clean snapshot
[ ] find / -newer /tmp/install-marker -type f    files modified since install
[ ] sha256sum /lib/modules/*/modules.dep         compare to known-good
```

### The arms race ladder

```
  Detection                           Evasion
  ----------------------------        ----------------------------
  lsmod                           ->  list_del from module list
  /sys/module/ cross-check        ->  kobject_del from sysfs
  /proc/kallsyms grep             ->  filter kallsyms reads
  ftrace enabled_functions        ->  intercept ftrace_enabled
  eBPF kprobe detection           ->  block bpf() syscall
  /proc/kcore memory scan         ->  filter kcore reads
  Volatility on /proc/kcore       ->  (defeated by hypervisor dump)
  QEMU monitor memory dump        ->  (no kernel evasion possible)
```

The final lesson: **the hypervisor always wins.** A rootkit inside a VM
cannot hide from the host reading its memory. This is why QEMU is the
perfect teaching environment. You ARE the hypervisor.

## Scoring

```
  Layer                      Detected?   Method used
  -----                      ---------   -----------
  Module hidden from lsmod      ?
  Module hidden from sysfs      ?
  /proc entry hidden            ?
  Credential backdoor           ?
  C2 connection                 ?
  Encryption identified         ?
  Boot persistence              ?
  Log scrubbing                 ?
                          Score:    /8
```

## Contract

The check script verifies:

- Module is hidden from `lsmod` and `/sys/module/`.
- `/proc/.rk` is hidden from `ls /proc/` but accessible by direct path.
- `/proc/.rk` contains `hidden=yes`, `persistence=`, and `escalation=`.
- `kill -63` is intercepted (signal hook, returns success).
- `/etc/modules-load.d/hello.conf` exists with `hello` (boot persistence).
- Blue team check: `stat /proc/.rk` reveals metadata even when hidden.

## Verify

```bash
./rootkitlings check capstone-5
```

## No hints

This is the final exam. If you need hints, go back to the lings.

## Further reading

- Volatility Foundation: hidden module detection
- Aqua Security Tracee: eBPF-based syscall hook detection
- rkchk: Rust-based rootkit detector (CR0.WP, MSR, syscall table checks)
- Singularity rootkit: state-of-the-art evasion (hooks ftrace, eBPF, kcore)
- Linux kernel rootkit taxonomy survey (arXiv 2304.00473)
