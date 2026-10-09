# Stage 4 architecture

Status: R15 is independently validated; R16 is implemented and awaits independent audit.

## Module-list decision

The proposed module-view suppression lesson is retired.
Linux v7.2 protects module-list writers with the internal `module_mutex` and RCU operations that an external module cannot join through an exported API.
Teaching a private-mutex `list_del()` as safe would be wrong.
Stage 4 instead changes bounded rendered results and always preserves an independent evidence path.

## R16: packed directory filter

Public alias: L15.
Stable ID: `packed-directory-filter`.
Title: Filter packed directory records safely.
Direct prerequisites: `return-aware-syscall-telemetry` and `cross-view-evidence`.

The learner owns only `instrumentation/dirent_filter.c`.
It implements one allocation-free helper returning `INVALID`, `UNCHANGED`, or `CHANGED`.
The helper validates the complete native Linux v7.2 `linux_dirent64` sequence before mutation, uses unaligned-safe field copies, matches one exact name, compacts with `memmove()`, propagates opaque `d_off` cookies, and fails open when every record matches.

The supplied integration registers a managed identity Kprobe for local `filldir64()` and a 16-instance return probe for exported `iterate_dir()`.
The return-probe entry sees the actual file used by enumeration and accepts only an enabled procfs root call whose callback actor equals the resolved native `filldir64()` address and whose initial count is at most 32 KiB.
On the pinned IBT-enabled x86 kernel, the identity helper reverses the architecture's four-byte function-entry Kprobe adjustment before comparing the actor.
This removes numeric-descriptor close/reuse races while explicitly depending on the private Linux v7.2 `getdents_callback64` layout.
After changed copy-back, it adjusts `current_dir`, `prev_reclen`, and `ctx.count`; the outer syscall then writes the final cookie and calculates the shortened result.
Copy-back failure clears `prev_reclen` and forces `-EFAULT` so the outer syscall cannot replace the error with a positive count.
The buffer copy-back remains nontransactional with concurrent userspace mapping changes.

The controlled object is `/proc/rootkitlings_hidden`, mode 0400, with exact content `present=1\n`.
Direct lookup remains available while mixed proc-root enumeration omits the name.
An ordinary directory with the same name remains unchanged.

Status adds:

```text
directory_filter=active
directory_target=rootkitlings_hidden
directory_chunk_capacity=32768
directory_returns_missed=0
```

Acquisition order is state, the inherited two-Kprobe set, the `filldir64` identity Kprobe, the `iterate_dir` return probe, the hidden proc entry, then public control/status.
Release and every rollback path use the exact reverse order.
Separate failures cover the missing getdents64 target, hidden publication, and public publication after all earlier resources exist.

Host verification compiles the exact learner helper with address and undefined-behavior sanitizers.
Its corpus covers valid, invalid, unaligned, overlapping, guarded, all-match, randomized, exact-name, and cookie-chain cases.
Hostile mutations prove that one-pass mutation, overlapping `memcpy()`, prefix matching, stale output length, false EOF, and missing cookie repair are rejected.

Guest verification uses raw `getdents64()` rather than `ls` alone.
It compares direct lookup, proc-root enumeration, an unrelated directory, Kprobe registry state, exact status, small read behavior, cookie seeks, isolated all-match progress, oversized fail-open behavior, concurrency, enable/disable, rollback, unload, and reload.

Primary implementation sources are Linux v7.2 `fs/readdir.c`, `include/linux/dirent.h`, `kernel/kprobes.c`, `Documentation/trace/kprobes.rst`, `include/linux/uaccess.h`, and `fs/proc/root.c`.
