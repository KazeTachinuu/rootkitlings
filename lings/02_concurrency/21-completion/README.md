# 21 - Completion

A finite worker needs a reliable way to announce that its last instruction has
finished. A delay guesses. A completion records the event.

```
  worker                              module exit
  +----------------------+            +-------------------------+
  | perform finite work  |            | wait_for_completion()   |
  | complete(&done)      |----------->| release worker-owned    |
  | return               |   event    | resources               |
  +----------------------+            +-------------------------+
```

## Build it

Create two finite kthreads. Put a `struct completion` beside each task and
initialize it before starting that task. Each worker:

1. performs its work;
2. increments the observable `finished` count;
3. calls `complete(&worker->done)` exactly once;
4. returns.

Module exit waits once for each worker before removing `/proc/rk_completion`.
Completions are event counters, not shared-data locks: the next lesson uses a
mutex to protect a multi-step update.

If the second `kthread_run()` fails, wait for the first worker before returning
from init. Its code belongs to the module and must not outlive a failed load.

## Contract

- `/proc/rk_completion` reports `finished=N`.
- Both finite workers eventually report completion.
- Unload waits for both completion events and removes the proc entry.
- The check polls the observable state with a timeout; it does not assume a
  particular scheduler delay.

```bash
./rootkitlings check 21
```

See also: [22 - Mutex](../22-mutex/README.md).
