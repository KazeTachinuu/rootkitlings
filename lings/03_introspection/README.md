# Introspection

Understand what the kernel knows about running processes, privileges, and symbols.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 24 | Current task | `current->comm`, `current->pid` in a callback. | Any process can trigger module code. No caller filtering. |
| 25 | Credentials | Read `current_cred()` uid/gid. | Credentials are immutable snapshots. Reading stale creds is a race. |
| 26 | Credential manipulation | `prepare_kernel_cred` + `commit_creds`. | Gives instant root. Detectable by audit log and UID 0 transitions. |
| 27 | Symbols | Resolve unexported kernel symbols at runtime. | The portable approach uses a temporary kprobe to read the address. Kprobe registration is visible in debugfs. |
| 28 | Kernel random | `get_random_bytes`, `get_random_u32`. | Predictable IDs and timings are fingerprintable. Randomness fixes this. |

Credential escalation (ling 26) is the classic rootkit backdoor: signal a
process, it becomes root. Diamorphine does this with `kill -63`. Every UID 0
transition is visible in the audit subsystem.

Ling 27 demonstrates symbol resolution through kprobe registration: register
the name, read `kp.addr`, then unregister. It depends on the target symbol and
the kernel's kprobe policy, and the registration is observable while active.
