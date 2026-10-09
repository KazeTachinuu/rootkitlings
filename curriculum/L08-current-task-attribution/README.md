# Attribute a proc read to the executing task

The proc record already reports module state.
Add the thread ID and command name of the task that produces that record:

```text
active=1
reader_tid=123
reader_comm="cat"
```

## The callback's `current` task

On Linux, `current` points to the task executing the kernel code at that moment.
Inside the proc show callback, that is the task whose read causes `seq_file` to produce the record.

It is not necessarily the task that opened the file.
An open descriptor can be inherited by another process or shared with another thread before its first read.

Use `task_pid_nr(current)` for `reader_tid`.
This reports the kernel thread ID of the executing task, so two threads in one process have distinct values.
Do not use the process-wide TGID.
This exercise does not translate the ID into the proc opener's PID namespace.

Use `get_task_comm()` to copy the command name into a local `char reader_comm[TASK_COMM_LEN]` array.
Do not read `current->comm` directly.
The helper gives you a bounded, terminated snapshot even if another task is renaming the thread.

Keep the task pointer, ID, and command snapshot local to the callback.
The callback uses `current` immediately, so it does not store a task pointer or take a task reference.

## A command name is hostile text

A Linux command name can contain a quote, a backslash, a newline, control bytes, or non-ASCII bytes.
Printing it with `%s` could add fake fields or break the quoted value.

`rk_print_quoted_comm()` is supplied.
Its output contract is:

- printable ASCII is copied as-is, except `"` and `\`
- `"`, `\`, control bytes, and non-ASCII bytes become lowercase `\xhh`
- the escaped result is enclosed in double quotes

For example, the bytes `a`, `"`, newline become `"a\x22\x0a"`.
The helper uses `seq_escape_mem()` with the Linux v7.2 behavior verified by the checker.
Do not replace it with raw `%s` output or a second escaping scheme.

## When the snapshot is taken

The first read materializes a complete `seq_file` record in a kernel buffer.
Later partial reads can be made by a different task, but they continue consuming that buffered record and keep its original attribution.

After a rewind to offset zero, the next read materializes a new record and takes a new snapshot of that reader.
This record therefore attributes materialization, not ownership of the open file and not every userspace read call.
It is observation data, not authentication or proof that the reader caused any earlier event.

## Your task

Edit only `interface/proc.c`.
In `rk_status_show()`:

1. copy `current` into one local task pointer
2. snapshot its thread ID with `task_pid_nr()`
3. snapshot its command name with `get_task_comm()`
4. print the exact three-line record shown above, using the supplied quoted-name
   helper

Keep the inherited state access, proc publication, and teardown unchanged.
Do not add global task data, a task reference, a lock, allocation, or a manual proc read callback.

## Check

Run:

```bash
./rootkitlings check
```

The checker uses an independent static userspace program.
It covers a descriptor opened before `fork()`, a hostile command name on a nonleader thread, a record consumed by another task after buffering, attribution after rewind, partial reads, repeated end of file, rejected writes, failure rollback, unload through an open descriptor, and two load cycles.

## Official references

- [Linux v7.2 `current` on x86](https://github.com/torvalds/linux/blob/v7.2/arch/x86/include/asm/current.h)
- [Linux v7.2 task and process ID helpers](https://github.com/torvalds/linux/blob/v7.2/include/linux/pid.h)
- [Linux v7.2 `TASK_COMM_LEN` and `get_task_comm()`](https://github.com/torvalds/linux/blob/v7.2/include/linux/sched.h)
- [Linux v7.2 string escaping rules](https://github.com/torvalds/linux/blob/v7.2/lib/string_helpers.c)
- [Linux v7.2 `seq_escape_mem()`](https://github.com/torvalds/linux/blob/v7.2/fs/seq_file.c)
- [The `seq_file` interface](https://docs.kernel.org/7.2/filesystems/seq_file.html)
