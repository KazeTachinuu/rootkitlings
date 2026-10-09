# Hide one name from a directory listing

An object can remain reachable by its exact path while a directory listing omits
its name.
Hide one controlled proc entry from native x86-64 directory listings without
changing the object or corrupting its neighbors.

Success proves that you can validate and compact a packed result safely.
`getdents64()` supplies that result as adjacent variable-length records.

The supplied module creates `/proc/rootkitlings_hidden`.
A direct read still returns:

```text
present=1
```

When actions are enabled, native x86-64 `getdents64()` calls on the procfs root
omit that name from mixed result chunks.
The object still exists, and other directories are unchanged.

## The record sequence

Linux v7.2 emits this native record layout:

```c
struct linux_dirent64 {
	u64 d_ino;
	s64 d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[];
};
```

`d_reclen` is the 8-byte-aligned size of the fixed fields, filename, and NUL:

```c
ALIGN(offsetof(struct linux_dirent64, d_name) + name_length + 1,
      sizeof(u64))
```

Validate the complete sequence before changing any byte.
For each record, require:

- Enough remaining bytes to reach `d_name`.
- A nonzero `d_reclen` contained in the remaining buffer.
- A nonempty NUL-terminated name inside the record.
- A name shorter than `PATH_MAX` with no `/`.
- The exact canonical aligned record length above.

The buffer may be unaligned, so read `d_reclen` and `d_off` with `memcpy()`.
Ignore padding after the first filename NUL.

## Compaction and directory cookies

Compare filename length and bytes exactly.
After validation, compact retained records with `memmove()`.

`d_off` is an opaque resume cookie.
For each removed record after a retained record, copy its `d_off` bytes into
that retained record; a removed run leaves its last cookie there.
Do not interpret or calculate cookies.

If every record matches, return the chunk unchanged.
Returning zero would falsely signal EOF after the kernel advanced position.

## Your task

Edit one file:

- `instrumentation/dirent_filter.c`

Implement:

```c
enum rk_dirent_filter_result
rk_filter_dirents64(void *buffer, size_t length,
		    const char *target, size_t target_length,
		    size_t *output_length);
```

Set `*output_length` to the input length before validation.
Return:

- `RK_DIRENT_FILTER_INVALID` for a malformed sequence, without changing the
  buffer.
- `RK_DIRENT_FILTER_UNCHANGED` when no record matches or every record matches,
  without changing the buffer.
- `RK_DIRENT_FILTER_CHANGED` after compacting a mix of removed and retained
  records and setting the shorter output length.

Bytes after the changed output length are unspecified.
The pure helper must not allocate memory, access userspace, read module state,
or know about probes.

Run:

```bash
./rootkitlings check
```

The checker sanitizes malformed and randomized streams, then tests the supplied
integration in the guest.

## Supplied integration and limits

The supplied `iterate_dir()` return probe receives the actual `struct file`.
A managed identity Kprobe resolves the pinned `filldir64()` and reverses x86's
`ENDBR64` adjustment before comparing the callback.
It admits only native `getdents64()` on an enabled procfs root with at most 32
KiB requested.

The private Linux v7.2 `getdents_callback64` layout is compile-time checked and
used only with that exact actor; it is not a stable module API.
Entry allocates bounded `GFP_ATOMIC` scratch and stores call data per instance.
Return copies with nofault helpers and validates counts and final boundaries
before filtering.
After changed copy-back, it adjusts `current_dir`, `prev_reclen`, and remaining
count so `getdents64()` produces the shorter result and final cookie.
Copy-back failure clears `prev_reclen` and forces `-EFAULT`.
The rewrite is not transactional against concurrent userspace buffer changes.

The following cases deliberately fail open:

- Legacy `getdents` and 32-bit compatibility calls.
- Other enumeration APIs and non-root or non-proc directories.
- Zero or larger-than-32-KiB requests.
- Scratch-allocation failure or an unavailable Kretprobe instance.
- A chunk in which every record has the target name.

`directory_returns_missed` reports Kretprobe instance exhaustion.
Allocation precedes admission, so a miss need not be a selected enumeration.

## Official Linux v7.2 references

- [Directory record layout, `filldir64()`, cookie writes, and `getdents64()`][1]
- [`struct linux_dirent64`][2]
- [Kretprobe instance data, capacity, and unregister behavior][3]
- [Linux v7.2 Kprobe implementation][4]
- [x86 Kprobe function-entry adjustment][5]
- [Nofault userspace-copy declarations][6]
- [Procfs root construction][7]
- [`getdents64(2)` and zero-at-EOF][8]

[1]: https://github.com/torvalds/linux/blob/v7.2/fs/readdir.c
[2]: https://github.com/torvalds/linux/blob/v7.2/include/linux/dirent.h
[3]: https://github.com/torvalds/linux/blob/v7.2/Documentation/trace/kprobes.rst
[4]: https://github.com/torvalds/linux/blob/v7.2/kernel/kprobes.c
[5]: https://github.com/torvalds/linux/blob/v7.2/arch/x86/kernel/kprobes/core.c
[6]: https://github.com/torvalds/linux/blob/v7.2/include/linux/uaccess.h
[7]: https://github.com/torvalds/linux/blob/v7.2/fs/proc/root.c
[8]: https://man7.org/linux/man-pages/man2/getdents64.2.html
