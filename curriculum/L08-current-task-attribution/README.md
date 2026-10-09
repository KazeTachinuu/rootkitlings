# Attribute a proc read to the executing task

Add the thread ID and command name of the task that materializes the proc
record:

```text
active=1
reader_tid=123
reader_comm="cat"
```

## The callback's `current` task

On Linux, `current` points to the task executing the kernel code at that moment.
In the show callback, it identifies the task whose read causes `seq_file` to
produce the record, not necessarily the opener.
A descriptor may be inherited or shared before its first read.

Use `task_pid_nr(current)` for `reader_tid`.
This reports the executing task's kernel thread ID, so sibling threads differ.
Do not use the process-wide TGID.
This exercise does not translate the ID into the proc opener's PID namespace.

Use `get_task_comm()` to copy the command name into a local
`char reader_comm[TASK_COMM_LEN]` array.
Do not read `current->comm` directly.
The helper provides a bounded, terminated snapshot during concurrent renaming.

Keep the task pointer, ID, and command snapshot local to the callback; immediate
use of `current` needs no task reference.

## A command name is hostile text

A command name may contain quotes, backslashes, newlines, control bytes, or
non-ASCII bytes, so raw `%s` output could forge fields.

`rk_print_quoted_comm()` is supplied.
Its output contract is:

- printable ASCII is copied as-is, except `"` and `\`
- `"`, `\`, control bytes, and non-ASCII bytes become lowercase `\xhh`
- the escaped result is enclosed in double quotes

For example, the bytes `a`, `"`, newline become `"a\x22\x0a"`.
The helper uses `seq_escape_mem()` with Linux v7.2 behavior.
Do not replace it with raw `%s` output or a second escaping scheme.

## When the snapshot is taken

The first read materializes a complete `seq_file` record in a kernel buffer.
Later partial reads consume that buffered record and keep its attribution, even
when another task performs them.

After a rewind to offset zero, the next read materializes a new record and takes
a new snapshot of that reader.
The record attributes materialization, not file ownership or every userspace
read, and is observation rather than authentication.

## Your task

Edit only `interface/proc.c`.
In `rk_status_show()`:

1. copy `current` into one local task pointer
2. snapshot its thread ID with `task_pid_nr()`
3. snapshot its command name with `get_task_comm()`
4. print the exact three-line record shown above, using the supplied quoted-name
   helper

Keep the inherited state access, proc publication, and teardown unchanged.
Do not add global task data, a task reference, a lock, allocation, or a manual
proc read callback.

## Check

Run:

```bash
./rootkitlings check
```

An independent checker proves attribution across `fork()`, threads, descriptor
sharing, buffering, and rewind.
It also checks hostile names, proc read semantics, rollback, unload through an
open descriptor, and repeated load cycles.

## Official references

- [Linux v7.2 `current` on x86][ref-1]
- [Linux v7.2 task and process ID helpers][ref-2]
- [Linux v7.2 `TASK_COMM_LEN` and `get_task_comm()`][ref-3]
- [Linux v7.2 string escaping rules][ref-4]
- [Linux v7.2 `seq_escape_mem()`][ref-5]
- [The `seq_file` interface][ref-6]

[ref-1]:
  https://github.com/torvalds/linux/blob/v7.2/arch/x86/include/asm/current.h

[ref-2]:
  https://github.com/torvalds/linux/blob/v7.2/include/linux/pid.h

[ref-3]:
  https://github.com/torvalds/linux/blob/v7.2/include/linux/sched.h

[ref-4]:
  https://github.com/torvalds/linux/blob/v7.2/lib/string_helpers.c

[ref-5]:
  https://github.com/torvalds/linux/blob/v7.2/fs/seq_file.c

[ref-6]:
  https://docs.kernel.org/7.2/filesystems/seq_file.html
