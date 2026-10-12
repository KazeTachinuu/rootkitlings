# M04: Call home and open a root shell

A rootkit that cannot communicate is a one-shot tool. This milestone
builds the callback agent: a kernel thread that connects out to the
operator, accepts commands over a length-framed TCP protocol, and opens
a root reverse shell on demand.

## The callback pattern

```text
  target                                     operator
  [agent thread]                             [listener :4444]
       |                                           |
       +-------- TCP connect --------------------->|
       |<------- length-framed commands -----------|
       |-------- length-framed replies ----------->|
       |                                           |
       |  on SHELL 9999:                           |
       |  [setsid/script/bash]                     |  [listener :9999]
       |       |                                   |        |
       |       +-------- TCP connect ---------------------->|
       |       |<------- interactive PTY ------------------>|
```

The rootkit initiates the connection, not the operator. This is the
standard connect-back (or "reverse shell") pattern. Advantages:

- Traverses NAT and most egress firewalls (outbound TCP is usually allowed)
- No listening port on the target to discover
- The operator controls when sessions start

## Beacon jitter

A regular reconnect interval is a signature. Real implants add
randomness to their retry timing:

```c
delay += get_random_u32_below(delay / 4 + 1);
```

The `+ 1` prevents calling `get_random_u32_below(0)` when `delay < 4`.
This adds up to 25% jitter on each retry, making the beacon timing
unpredictable to network monitors.

## Exponential backoff

On connection failure, the delay doubles up to `retry_max_ms`. On
successful connection, it resets to `retry_base_ms`. This avoids
hammering the operator's listener while still reconnecting quickly
when the operator is available.

## The wire protocol

Commands are length-framed (2-byte big-endian length prefix, max 64
bytes payload). Supported commands:

| Command | Reply | Effect |
|---------|-------|--------|
| PING | PONG | Keepalive |
| INFO | INFO rootkitlings | Identify the implant |
| STATUS | STATUS connected=... attempts=... | Agent state |
| SHELL port | OPEN request_id cookie | Open reverse shell to operator:port |
| SHELL STATUS | STATE request_id state result | Shell session state |
| QUIT | (closes) | End session gracefully |

## Root shell via UMH

The shell uses `call_usermodehelper` to spawn `/bin/bash` as root, with
stdin/stdout/stderr wired to a TCP socket back to the operator. The
operator starts a listener, sends `SHELL <port>`, and gets an
interactive root Bash session.

## Your task

1. In `core/main.c`: wire the init/exit sequence (init the shell, start
   the agent, tear down in reverse order on exit)
2. In `network/agent.c`: implement the command handler switch and add
   jitter to the retry delay

Run:

```sh
./rootkitlings check
```

## References

- [Linux v7.2 kernel socket API][1]
- [Linux v7.2 `call_usermodehelper`][2]

[1]: https://github.com/torvalds/linux/blob/v7.2/include/linux/net.h
[2]: https://github.com/torvalds/linux/blob/v7.2/include/linux/umh.h
