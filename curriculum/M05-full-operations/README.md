# M05: Full Operations

Integrate all commands over the encrypted callback agent with automatic OPSEC.

Every command from M01's local control now works remotely over the M03 secure
channel.
The agent automatically hides its own kthread PID and connection port on
connect, and cleans up on disconnect.

## Your task

Your starter is the complete L26 solution.
Verify that all 20+ commands work over the encrypted callback channel, and that
auto-OPSEC engages on connect and releases on disconnect.

Run:

```sh
./rklings check
```
