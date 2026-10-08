# 22 - Protect shared state with a mutex

A mutex gives one thread at a time exclusive access to a critical section.
Use it in process context when an operation spans multiple accesses or may later need to sleep.

## Observe the invariant

Two workers each increment the same counter 10,000 times.
The required final value is exactly 20,000.

```text
thread A -- lock -- read + increment + write -- unlock --+
                                                       counter
thread B -- waits -- lock -- read + increment + write --+
```

`shared_counter++` is a read-modify-write sequence, not one indivisible operation.
Without the mutex, both CPUs can read the same old value and one increment is lost.

## How it works

`DEFINE_MUTEX(counter_lock)` creates a statically initialized mutex.
`mutex_lock()` may sleep, so it is invalid in timer, interrupt, and probe contexts.
`mutex_unlock()` ends the protected region.

Every access participating in the invariant uses the same lock.
The proc reader therefore locks around its read as well as workers locking around writes.

## Implement it

Lock `counter_lock` immediately before `shared_counter++` and unlock it immediately afterward.

## Verify

```bash
./rootkitlings start L22
$EDITOR module/hello.c
./rootkitlings check L22
```

The check waits for the workers and requires the exact final value.
