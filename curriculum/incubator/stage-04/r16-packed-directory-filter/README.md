# Filter packed directory records safely

`getdents64()` returns a byte buffer containing adjacent variable-length records.
This exercise removes one exact name from one controlled enumeration result without corrupting the remaining records.

The supplied module creates `/proc/rootkitlings_hidden`.
A direct read still returns:

```text
present=1
```

When actions are enabled, native x86-64 `getdents64()` calls on the procfs root omit that name from mixed result chunks.
The object still exists, and a normal directory containing the same name is unchanged.
You are changing one view, not removing an object or making it invisible.

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

`d_reclen` advances to the next record.
It must equal the 8-byte-aligned size of the fixed fields, the filename, and its terminating NUL:

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

The buffer itself may be unaligned.
Read `d_reclen` and `d_off` with `memcpy()` instead of dereferencing an unaligned structure pointer.
Ignore padding after the first filename NUL.

## Compaction and directory cookies

Compare both filename length and bytes.
Prefixes, suffixes, substrings, and case variants are different names.

After validation, make a second pass and move retained records forward with `memmove()`.
The source and destination can overlap.

`d_off` is an opaque directory cookie used to resume enumeration.
Linux writes the next position into the preceding record.
When a record is removed after a retained record, copy the removed record's `d_off` bytes into that previous retained record.
A run of removed records therefore leaves the previous retained record with the last removed cookie.
Do not interpret or calculate cookies.

If every record matches, return the chunk unchanged.
Changing a positive syscall result to zero would look like end-of-directory even though the kernel already advanced the file position.
Failing open avoids that false EOF.

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

- `RK_DIRENT_FILTER_INVALID` for a malformed sequence, without changing the buffer.
- `RK_DIRENT_FILTER_UNCHANGED` when no record matches or every record matches, without changing the buffer.
- `RK_DIRENT_FILTER_CHANGED` after compacting a mix of removed and retained records and setting the shorter output length.

Bytes after the changed output length are unspecified.
The pure helper must not allocate memory, access userspace, read module state, or know about probes.

Run:

```bash
./rootkitlings check
```

The checker compiles this exact helper with address and undefined-behavior sanitizers, tests malformed and randomized record streams, then verifies the supplied integration in the disposable guest.

## Supplied integration and limits

The supplied return probe targets exported `iterate_dir()` on Linux v7.2.
Its entry handler sees the actual `struct file` used for enumeration, so concurrent descriptor reuse cannot make it classify a different file.
A managed identity Kprobe resolves the pinned kernel's local `filldir64()` address.
On the pinned x86 kernel with indirect-branch tracking, Kprobes moves its probe past the four-byte `ENDBR64`; the supplied identity helper reverses that documented architecture adjustment before comparing the callback pointer.
The return probe admits only the native `getdents64()` callback with that exact actor, an enabled procfs root file, and an initial buffer count no larger than 32 KiB.

The integration depends on the private Linux v7.2 `getdents_callback64` layout.
That pinned layout is declared in the supplied file and checked through the callback actor identity; it is not a stable module API.
The title's safety claim applies to the pinned contract and complete record validation, not arbitrary kernel versions.

The entry handler allocates the exact bounded size with `GFP_ATOMIC` and stores the initial userspace pointer, capacity, and callback in one Kretprobe instance.
The return handler copies the produced prefix with nofault helpers, calls your pure filter, and copies back only a changed prefix.
It then adjusts `current_dir`, `prev_reclen`, and the remaining count so `getdents64()` itself writes the final cookie and computes the shorter return value.
If copy-back fails, it clears `prev_reclen` and forces `-EFAULT`, preventing the outer syscall from replacing that error with a positive count.
This rewrite is not transactional: another userspace thread can change, unmap, or protect the buffer between the syscall and return handler.

The following cases deliberately fail open:

- Legacy `getdents` and 32-bit compatibility calls.
- Other enumeration APIs and non-root or non-proc directories.
- Zero or larger-than-32-KiB requests.
- Scratch-allocation failure or an unavailable Kretprobe instance.
- A chunk in which every record has the target name.

`directory_returns_missed` reports Kretprobe instance exhaustion.
Instance allocation happens before the entry handler rejects an out-of-scope call, so a miss alone does not prove that a selected proc enumeration was lost.

## Official Linux v7.2 references

- [Directory record layout, `filldir64()`, cookie writes, and `getdents64()`](https://github.com/torvalds/linux/blob/v7.2/fs/readdir.c)
- [`struct linux_dirent64`](https://github.com/torvalds/linux/blob/v7.2/include/linux/dirent.h)
- [Kretprobe instance data, capacity, and unregister behavior](https://github.com/torvalds/linux/blob/v7.2/Documentation/trace/kprobes.rst)
- [Linux v7.2 Kprobe implementation](https://github.com/torvalds/linux/blob/v7.2/kernel/kprobes.c)
- [x86 Kprobe function-entry adjustment](https://github.com/torvalds/linux/blob/v7.2/arch/x86/kernel/kprobes/core.c)
- [Nofault userspace-copy declarations](https://github.com/torvalds/linux/blob/v7.2/include/linux/uaccess.h)
- [Procfs root construction](https://github.com/torvalds/linux/blob/v7.2/fs/proc/root.c)
- [`getdents64(2)` and zero-at-EOF](https://man7.org/linux/man-pages/man2/getdents64.2.html)
