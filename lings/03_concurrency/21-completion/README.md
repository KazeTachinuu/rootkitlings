# 21 - Wait for finite work to finish

A completion represents a one-time “finished” event.
Use it when one execution path must wait until another path reaches a known endpoint.

## Observe it

The module starts two finite workers.
Each worker signals its own completion before returning.
The proc entry counts which completion objects are done.

```text
worker 0 ---- work ---- complete(done[0]) ---+
                                               +--> unload may continue
worker 1 ---- work ---- complete(done[1]) ---+
```

## How it works

`init_completion()` initializes the event before the worker can access it.
`complete()` records one available completion and wakes a waiter.
`wait_for_completion()` sleeps until it can consume that completion.

A completion carries event state, unlike a bare wakeup.
If the worker signals before teardown starts waiting, the later wait still returns.
One `complete()` matches one `wait_for_completion()`.

Initialization rollback waits only for workers that were successfully created.
Module exit waits for both workers before their code can be unloaded.

## Implement it

Call `complete(&worker->done)` when the worker finishes its simulated work.

## Verify

```bash
./rootkitlings start L21
$EDITOR .rootkitlings/work/completion/hello.c
./rootkitlings check L21
```

The check waits for the reported result instead of assuming a scheduling delay.
