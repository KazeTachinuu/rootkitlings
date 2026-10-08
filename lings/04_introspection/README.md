# Introspection

Kernel callbacks can inspect the task that entered them, including its identity and credentials.
This section moves from observing that caller to changing its credentials, resolving a runtime symbol, and generating unpredictable state.

| # | Mechanism | Practical result |
|--:|-----------|------------------|
| 24 | `current` | Attribute a proc callback to the process performing the read. |
| 25 | `current_cred()` | Report namespace-aware UID and GID values. |
| 26 | `prepare_creds()` and `commit_creds()` | Replace every identity role of the calling task. |
| 27 | Temporary named kprobe | Resolve a probeable symbol address at runtime. |
| 28 | Kernel random API | Generate a session ID and token. |

Read task and credential state only where it is valid.
`current` changes with execution context, credential pointers are immutable snapshots, and retained task state needs explicit lifetime management.

Runtime symbol resolution depends on kernel version, probe policy, and whether the target is probeable.
Random identifiers reduce predictability but do not conceal state after it is exposed.
