# Capstones

Lings isolate one mechanism. Capstones combine only mechanisms already taught.
They add integration pressure - state ownership, rollback, concurrency, and clean
unload - but no surprise API.

| # | After | Capstone | Integration goal |
|--:|------:|-----------|------------------|
| 0 | 16 | [Config store](0-config-store/README.md) | procfs, lists, allocation, userspace helper |
| 1 | 23 | [Beacon](1-beacon/README.md) | kthread, wait/sleep, parameter callback, mutex |
| 2 | 35 | [Tripwire](2-tripwire/README.md) | probe lifecycle and bounded event storage |
| 3 | 42 | [Dead drop](3-dead-drop/README.md) | hidden procfs interface and trigger |
| 4 | 51 | [Implant](4-implant/README.md) | kernel socket, command loop, reconnect lifecycle |
| 5 | 59 | [Red vs blue](5-red-vs-blue/README.md) | hiding, escalation, persistence, detection |

The sequence is cumulative in engineering discipline, not in source code. Each
capstone stands alone and should initialize in dependency order, unwind in
reverse order, and leave no worker or hook active after module exit.

## Workflow

```bash
./rootkitlings readme capstone-0
./rootkitlings check  capstone-0
./rootkitlings peek   capstone-0
```
