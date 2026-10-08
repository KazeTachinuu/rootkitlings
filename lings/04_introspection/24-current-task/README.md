# 24 - Identify the current task

`current` points to the `task_struct` of the task executing the current kernel code.
Use it when a callback must attribute an operation to its caller.

## Observe it

Reading `/proc/rk_task` reports the reader, not the process that loaded the module.

```bash
cat /proc/rk_task
# pid=<cat-pid> comm=cat
```

```text
cat enters read() ---> proc callback runs ---> current is cat
```

## How it works

`current->pid` is the task ID visible to the kernel.
`current->comm` is the fixed-size command name and may be shorter than the executable path.
The value of `current` is contextual and must be read in the callback where attribution matters.
Saving a `task_struct *` beyond that context requires explicit lifetime handling.

## Implement it

Print `current->pid` and `current->comm` as `pid=<number> comm=<name>` in `info_show()`.

## Verify

```bash
./rootkitlings start L24
$EDITOR .rootkitlings/work/current-task/hello.c
./rootkitlings check L24
```

The check compares the reported PID and name with the actual process performing the read.
