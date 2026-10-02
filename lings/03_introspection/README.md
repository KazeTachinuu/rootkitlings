# Introspection

Understand what the kernel knows about running processes, privileges, and symbols.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 21 | Current task | `current->comm`, `current->pid` in a callback. | Any process can trigger module code. No caller filtering. |
| 22 | Credentials | Read `current_cred()` uid/gid. | Credentials are immutable snapshots. Reading stale creds is a race. |
| 23 | Credential manipulation | `prepare_kernel_cred` + `commit_creds`. | Gives instant root. Detectable by audit log and UID 0 transitions. |
| 24 | Symbols | Resolve unexported kernel symbols at runtime. | The portable approach uses a temporary kprobe to read the address. Kprobe registration is visible in debugfs. |
| 25 | Kernel random | `get_random_bytes`, `get_random_u32`. | Predictable IDs and timings are fingerprintable. Randomness fixes this. |

Credential escalation (ling 23) is the classic rootkit backdoor: signal a
process, it becomes root. Diamorphine does this with `kill -63`. Every UID 0
transition is visible in the audit subsystem.

Ling 24 teaches symbol resolution via the kprobe trick: register a kprobe
on the symbol name, read `kp.addr`, unregister. Works on any kernel with
kprobes enabled. Every modern rootkit uses this pattern.
