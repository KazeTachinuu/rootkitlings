# Stage 4 architecture

Status: R15 through R17 are independently validated.

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
Compile-time field-offset and size checks guard that pinned layout, and exact callback-actor identity confines the private cast to native `getdents64()` calls.
Before mutation, the return handler also requires the remaining count and final-record boundary to agree with the produced byte range.
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
Separate failures cover the missing `iterate_dir` target, hidden publication, and public publication after all earlier resources exist.

Host verification compiles the exact learner helper with address and undefined-behavior sanitizers.
Its corpus covers valid, invalid, unaligned, overlapping, guarded, all-match, randomized, exact-name, and cookie-chain cases.
Hostile mutations prove that one-pass mutation, overlapping `memcpy()`, prefix matching, stale output length, false EOF, and missing cookie repair are rejected.

Guest verification uses raw `getdents64()` rather than `ls` alone.
It compares direct lookup, proc-root enumeration, an unrelated directory, Kprobe registry state, exact status, small read behavior, cookie seeks, isolated all-match progress, oversized fail-open behavior, concurrency, enable/disable, rollback, unload, and reload.

Primary implementation sources are Linux v7.2 `fs/readdir.c`, `include/linux/dirent.h`, `kernel/kprobes.c`, `Documentation/trace/kprobes.rst`, `include/linux/uaccess.h`, and `fs/proc/root.c`.

## R17: bounded read-result replacement

Public alias: L16.
Stable ID: `bounded-rendered-filter`.
Title: Replace bytes in one bounded read result.
Direct prerequisites: `return-aware-syscall-telemetry` and `cross-view-evidence`.

The learner owns only `instrumentation/replace.c`.
Its allocation-free helper scans a byte buffer without string termination, replaces complete non-overlapping equal-width patterns, and returns the occurrence count.
Zero width and widths larger than the result change nothing.
A match advances by the full width, so replacement bytes are not scanned again.

The supplied integration opens `/tmp/rootkitlings-read-target` during initialization, retains its inode, and closes the temporary file.
Hard links and renames therefore remain selected, while a new inode created at the original path is not selected until reload.
It registers a 32-instance named return probe on the Linux v7.2 `vfs_read()` ABI.
Each instance stores the user buffer and a 512-byte scratch buffer; probe callbacks allocate nothing.

Only a positive actual result of at most 512 bytes is copied without faults and passed to the learner helper.
The request size does not decide eligibility.
A result larger than 512 bytes is unchanged and increments `oversize_results`.
A pattern split across separate results is unchanged.
Zero matches avoid copy-back.
Successful copy-back adds the occurrence count to `redactions`.
Either copy failure increments `copy_failures`, returns `-EFAULT`, and adds no redactions.
Copy-back remains nontransactional if userspace changes its mapping concurrently.

The read-only `/proc/rootkitlings` endpoint reports:

```text
limit=512
redactions=N
oversize_results=N
copy_failures=N
return_capacity=32
returns_missed=N
```

The Kretprobe reserves an instance before its entry handler rejects unrelated files, so `returns_missed` can include unrelated `vfs_read()` calls.
The module has no control toggle.

This mechanism changes only the `vfs_read()` result path.
Ordinary `read` and `pread64` use that path on the pinned kernel.
Vectored reads, memory mappings, `sendfile`, splice and copy paths, in-kernel reads, and results over the limit remain independent views of the original bytes.
It is neither access control nor reliable concealment.

Acquisition order is retained inode, return probe, then proc publication.
Release and rollback use the exact reverse order.
Separate failures cover preflight rejection, a missing path, a missing probe target, proc publication, copy-in, and copy-out.

Host verification runs the learner helper under address and undefined-behavior sanitizers.
It covers every length through 512, embedded NUL bytes, first, last, adjacent and overlapping candidates, alternate patterns and widths, red zones, guard pages, generated reference cases, and replacement text containing the pattern.
Hostile mutations prove that string-only, hard-coded, off-by-one, overlapping, rescanning, and out-of-range implementations are rejected.

Guest verification exercises raw `read`, `pread64`, `readv`, `preadv`, mapping, and `sendfile` paths; binary data; exact 512- and 513-byte results; a short result from a large request; split patterns; inode identity; deterministic copy failures; exact proc reads; registry state; concurrency; unload; and reload.

Primary implementation sources are Linux v7.2 `fs/read_write.c`, `fs/splice.c`, `mm/filemap.c`, `mm/maccess.c`, `include/linux/kprobes.h`, `kernel/kprobes.c`, `Documentation/trace/kprobes.rst`, and `arch/x86/include/asm/ptrace.h`.
