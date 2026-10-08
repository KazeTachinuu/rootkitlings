# 19 - Run a persistent kernel thread

A kernel thread owns a long-running loop with process context and an explicit stop request.
Use one for background activity that needs its own control flow instead of isolated work items.

## Observe it

The thread appears in the process list and increments `/proc/rk_thread` once per second.

```bash
ps | grep '\[rk_thread\]'
cat /proc/rk_thread
sleep 2
cat /proc/rk_thread
```

Kernel thread names appear in square brackets because they have no userspace executable image.

## How it works

```text
kthread_create() ---> stopped task ---> wake_up_process() ---> heartbeat_loop()
                                                                |
module unload ---> kthread_stop() ---> kthread_should_stop() ----+
```

`kthread_create()` returns a `task_struct *` or an encoded error pointer.
Check it with `IS_ERR()` and recover the error with `PTR_ERR()`.
`wake_up_process()` starts the newly created thread.
The loop must check `kthread_should_stop()` so `kthread_stop()` can wait for an orderly return.

The thread is the counter's only writer, while procfs reads it concurrently.
Use `READ_ONCE()` and `WRITE_ONCE()` so both contexts perform one visible access instead of racing through compiler-generated accesses.
This does not make a compound update safe for multiple writers; the mutex and atomic exercises cover that case next.

`ssleep(1)` is simple periodic waiting but cannot react immediately to arbitrary events.
A wait queue provides event-driven wakeups.

## Implement it

After the existing creation and error check, wake `heartbeat_thread` with `wake_up_process()`.

## Verify

```bash
./rootkitlings start L19
$EDITOR .rootkitlings/work/kernel-thread/hello.c
./rootkitlings check L19
```

The check observes the named task, its increasing counter, and its disappearance after unload.
