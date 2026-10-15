# M05: Full Operations

Integrate all commands over the encrypted agent connection with automatic OPSEC.

Every command from M01's local control now works remotely over the M03 secure
channel.
The agent automatically hides its own PID and connection port on connect.

## Your task

Combine M04's remote command dispatch with L26's automatic OPSEC cleanup.
On connect, the agent must automatically hide its own kthread PID and its
listening port so the operator never has to issue those commands manually.

1. The agent loop starts with the L26 auto-targeting sequence.
2. All M04 remote commands remain functional.
3. On disconnect or module unload, OPSEC targets are released.

Run:

```sh
./rklings check
```
