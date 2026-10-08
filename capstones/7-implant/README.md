# Connect-back task agent

A listening backdoor waits at a fixed port and is easy to find.

A connect-back agent reverses the direction: it periodically contacts an operator, handles one session, and reconnects after the session ends.

This agent accepts four bounded text commands:

```text
PING             -> PONG
INFO             -> kernel=<release>
MARK <label>     -> create /tmp/rk_mark_<label>
QUIT             -> close the session
```

`MARK` provides a visible result without executing an arbitrary shell command.

It invokes `/bin/touch` with a fixed argument vector and waits for the helper to finish.

## Operate it

Start the exercise VM.
Its declared environment enables host networking:

```sh
./rootkitlings run C07
```

Start a listener on the host:

```sh
nc -l -p 4444
```

Load the module in the VM:

```sh
insmod hello.ko target=10.0.2.2 port=4444 interval=2
```

Type commands in the host listener:

```text
PING
PONG
INFO
kernel=7.2.5-4-omarchy
MARK nightfall
MARKED /tmp/rk_mark_nightfall
QUIT
```

Back in the VM:

```sh
ls -l /tmp/rk_mark_nightfall
cat /proc/rk_agent
```

The agent reconnects after `interval` seconds, so starting another listener opens a new session.

## Connection lifecycle

```text
                    operator unavailable
                           |
                           v
                    +-------------+
             +----->| wait interval|
             |      +------+------+
             |             |
             |             v
        disconnect    create + publish socket
             ^             |
             |             v
        +----+-----+     connect
        | session  |<-------+
        | tasks    |
        +----------+
```

The worker publishes the socket before `kernel_connect()`.

Module removal can therefore shut down a connection attempt or blocked receive before joining the thread.

The worker remains the sole owner responsible for releasing the socket.

`send_all()` loops because one TCP send may accept fewer bytes than requested.

`receive_line()` drains an oversized line through its newline and rejects it as one frame instead of executing its tail as another command.

## Helper boundary

`MARK nightfall` becomes an argument vector, not a shell expression:

```c
char *argv[] = {
        "/bin/touch",
        "/tmp/rk_mark_nightfall",
        NULL,
};
```

The label accepts only letters, digits, `-`, and `_` and is capped at 32 characters.

No shell parses the label, and no temporary output file is involved.

## Files

```text
main.c       parameters, status entry, acquisition and rollback
beacon.c     connect, framed session, retry, shutdown and counters
command.c    task validation, replies and userspace helper
implant.h    bounded protocol and component contracts
```

## Your work

`beacon_loop()` currently attempts one session and then exits.

Turn it into the lifecycle shown above.

Continue until `kthread_should_stop()` becomes true.

After every failed connection or completed session, wait on `retry_wait` for `retry_interval * HZ`.

Do not sleep after a stop request.

Do not release a socket from `beacon_stop()`; shutdown wakes the worker, and the worker releases what it owns.

## Verify

```sh
./rootkitlings check C07
```

The check rejects invalid parameters, completes two separate dynamic sessions, verifies the created mark, confirms the counters, and removes the module while a third session is blocked in receive.

The thread, proc entry, TCP connection, userspace helper, and created mark are all observable evidence.
