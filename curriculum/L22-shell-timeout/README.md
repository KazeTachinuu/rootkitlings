# Add shell timeouts and an exec slot

A shell that stays open forever is an OPSEC liability.
If the operator disconnects or forgets to close the session, the shell process
and its socket sit there waiting for a defender to notice.
Add an inactivity timeout using the kernel's delayed work mechanism, and a
second shell slot for one-shot command execution.

## The problem

Without a timeout, a reverse shell opened by the rootkit will persist
indefinitely.
That means:

- An idle TCP connection visible in `ss` or `netstat` output
- A `/bin/sh` process consuming a PID slot
- Evidence accumulating the longer the session lives

The fix: automatically kill the shell after a period of inactivity.

## Delayed work

The kernel workqueue subsystem provides delayed work items.
A delayed work item is scheduled to run after a specified number of jiffies:

```text
INIT_DELAYED_WORK(&work, callback_fn)
      |
      v
mod_delayed_work(system_wq, &work, delay_jiffies)
      |                                 |
      v                                 v
  [queued] -------- delay ---------> [callback runs]
      |
      v
cancel_delayed_work_sync(&work)   <-- blocks until callback finishes
```

Key functions:

```c
#include <linux/workqueue.h>

INIT_DELAYED_WORK(&dwork, fn);
mod_delayed_work(system_wq, &dwork, delay);
cancel_delayed_work_sync(&dwork);
```

`mod_delayed_work` resets the timer if the work is already pending.
This is exactly what you need for an inactivity timeout: every time the shell
sends or receives data, call `mod_delayed_work` to push the deadline forward.

## Shell structure

Define a shell structure that tracks state, the socket, and the timeout:

```c
struct rk_shell {
    struct mutex         lock;
    bool                 busy;
    struct socket       *sock;
    struct delayed_work  timeout_work;
    unsigned long        timeout_jiffies;
};
```

Create two instances: `interactive_shell` for a persistent `/bin/sh -i` session,
and `exec_shell` for one-shot `/bin/sh -c <cmd>` execution.
Each has its own mutex and timeout.

## Timeout callback

The timeout function fires when the timer expires without being reset:

```c
static void rk_shell_timeout_fn(struct work_struct *work)
{
    struct rk_shell *shell = container_of(
        to_delayed_work(work), struct rk_shell, timeout_work);

    mutex_lock(&shell->lock);
    if (shell->busy) {
        /* Kill the shell: shutdown socket, release, mark idle */
    }
    mutex_unlock(&shell->lock);
}
```

When the timeout fires, shut down the socket with `kernel_sock_shutdown`.
This causes the shell process's reads and writes to fail, which terminates it.

## Activity reset

Every time data passes through the shell (a command sent, output received),
reset the inactivity timer:

```c
mod_delayed_work(system_wq, &shell->timeout_work, shell->timeout_jiffies);
```

This pushes the timeout deadline forward by `timeout_jiffies` from now.

## Exec slot

The exec slot works like the interactive slot, but runs a single command and
auto-closes when the command finishes.
It does not need the same timeout behavior (the command has a natural end), but
should still have a maximum execution time to prevent hangs.

## Default timeout

Set the default timeout to 60 seconds:

```c
shell->timeout_jiffies = 60 * HZ;
```

`HZ` is the kernel's tick rate (typically 250 or 1000), so `60 * HZ` gives 60
seconds regardless of the actual tick frequency.

## Cleanup

In `rk_exit`, cancel both delayed works before destroying any state:

```c
cancel_delayed_work_sync(&interactive_shell.timeout_work);
cancel_delayed_work_sync(&exec_shell.timeout_work);
```

`cancel_delayed_work_sync` blocks until any running callback completes,
preventing use-after-free.

## Your task

Edit `execution/shell.c`:

1. Define `struct rk_shell` with lock, busy flag, socket, delayed_work, and
   timeout_jiffies.
2. Create two static instances: `interactive_shell` and `exec_shell`.
3. Implement `rk_shell_timeout_fn` to kill the shell on timeout.
4. Call `mod_delayed_work` on every shell activity to reset the timer.
5. Implement the exec slot for one-shot command execution.
6. In `rk_shells_init`, initialize both with `INIT_DELAYED_WORK`.
7. In `rk_shells_cleanup`, call `cancel_delayed_work_sync` for both shells.

Edit `core/init.c` to call `rk_shells_init` and `rk_shells_cleanup` during
module lifecycle.

Edit `network/agent.c` to wire shell dispatch into the session loop.

Run:

```sh
./rklings check
```

## References

- [Workqueue API](https://docs.kernel.org/core-api/workqueue.html)
- [`mod_delayed_work`](https://github.com/torvalds/linux/blob/v7.2/kernel/workqueue.c)
- [`cancel_delayed_work_sync`](https://github.com/torvalds/linux/blob/v7.2/include/linux/workqueue.h)
