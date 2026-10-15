# Add a second shell slot for exec

The interactive shell from L16 stays connected until the peer disconnects or the
timeout fires. That is the right tool for exploration, but scripted operations
need something different: connect, run one command, stream the output, and
disconnect.

This lesson adds a second shell slot. The interactive slot keeps its existing
behavior. The exec slot runs a single command and tears down the connection when
the command exits.

## Two shell slots

```text
mkdir rklings.shell          mkdir rklings.exec.CMD
      |                            |
      v                            v
 interactive_slot              exec_slot
      |                            |
  connect socket              connect socket
      |                            |
  /bin/sh -i                  /bin/sh -c "CMD"
  (stays open)                (runs, streams, exits)
      |                            |
  peer disconnects            disconnect automatically
```

Both slots share the same struct:

```c
struct rk_shell_slot {
    struct socket *sock;
    struct file   *file;
    struct mutex   lock;
    struct work_struct work;
    bool busy;
    char cmd[RK_CMD_MAX];
};
```

The `cmd` field is unused for the interactive slot. For the exec slot it holds
the command string extracted from the mkdir path.

## The mutex and busy flag

A shell slot must reject concurrent requests. The pattern is:

1. `mutex_lock(&slot->lock)`
2. Check `slot->busy`. If true, unlock and return `-EBUSY`.
3. Set `slot->busy = true`.
4. Copy any parameters (command string, address, port).
5. `mutex_unlock(&slot->lock)`
6. Queue the work item with `schedule_work(&slot->work)`.
7. Return 0 to the mkdir handler.

When the work function finishes:

1. `mutex_lock(&slot->lock)`
2. Set `slot->busy = false`, clear `slot->sock`, clear `slot->file`.
3. `mutex_unlock(&slot->lock)`

The mkdir handler returns immediately. The actual connection and shell execution
happen asynchronously on the system workqueue.

## Interactive slot: /bin/sh -i

The interactive shell uses the same technique as L16. The work function:

1. Creates a TCP socket with `sock_create_kern`.
2. Connects to the configured peer address and port.
3. Wraps the socket in a `struct file` with `sock_alloc_file`.
4. Calls `call_usermodehelper_setup` with `/bin/sh` and argv `{"/bin/sh", "-i", NULL}`.
5. The init callback installs the file as fd 0, 1, and 2.
6. Executes with `call_usermodehelper_exec(..., UMH_WAIT_PROC)`.
7. Cleans up the file and socket on every path.

## Exec slot: /bin/sh -c CMD

The exec slot follows the same connection steps. The difference is in the argv
and the lifecycle:

```c
char *argv[] = { "/bin/sh", "-c", slot->cmd, NULL };
```

`UMH_WAIT_PROC` makes `call_usermodehelper_exec` block until the child process
exits. The shell runs the command, its stdout and stderr flow through the
socket, and when the process terminates the work function disconnects.

The caller receives the entire output over the socket, then observes the
connection close as the end-of-output signal.

## Installing file descriptors

The `call_usermodehelper_setup` init callback runs in the context of the new
userspace process, before it execs the binary. This is where you install the
socket file as stdin, stdout, and stderr:

```c
static int rk_shell_init_fn(struct subprocess_info *info,
                            struct cred *new)
{
    struct rk_shell_slot *slot = info->data;
    unsigned int fd;
    int error;

    for (fd = 0; fd < 3; fd++) {
        error = rk_install_fd(slot->file, fd);
        if (error)
            return error;
    }
    return 0;
}
```

`rk_install_fd` calls `get_unused_fd_flags(0)`, verifies the returned
descriptor matches the expected number, then calls `get_file` followed by
`fd_install`.

## Your task

Edit `main.c`. The interactive slot already works. Add:

1. The exec slot work function that connects, runs `/bin/sh -c CMD`, and
   disconnects.
2. The mkdir dispatch for `rklings.exec.CMD` that copies the command into the
   exec slot and queues its work.

Run:

```sh
./rklings check
```

## References

- Linux v7.2 user-mode helpers
  <https://github.com/torvalds/linux/blob/v7.2/kernel/umh.c>
- Linux v7.2 `call_usermodehelper_setup`
  <https://github.com/torvalds/linux/blob/v7.2/include/linux/umh.h>
- Linux v7.2 file descriptor installation
  <https://github.com/torvalds/linux/blob/v7.2/fs/file.c>
