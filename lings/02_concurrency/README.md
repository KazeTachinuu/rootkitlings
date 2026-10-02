# Concurrency

Defer work, run background threads, and protect shared state.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 16 | Timer | `timer_setup` + `mod_timer` for delayed work. | A timer still firing during rmmod crashes the kernel. |
| 17 | Workqueue | Queue deferred work, cancel before unload. | Unfinished work after `cancel_work_sync` is a use-after-free. |
| 18 | Kernel thread | `kthread_create`, `kthread_stop`, join. | A thread that ignores `kthread_should_stop` blocks rmmod forever. |
| 19 | Waiting | `wait_event_interruptible` + `wake_up`. | Busy-polling wastes CPU. Sleeping without wake condition deadlocks. |
| 20 | Shared state | Two writers, no lock. Reproduce a lost update. | Counter incremented 10000x by two threads reads less than 20000. |
| 21 | Mutex | `mutex_lock`/`mutex_unlock` around the shared counter. | Mutex in interrupt context deadlocks the kernel. |
| 22 | Atomic | `atomic_t` for a single transition flag. | Atomics protect one variable, not a multi-field invariant. |

Timers and workqueues power beaconing (periodic callback to the controller).
Kthreads run the beacon loop. Mutexes protect the task queue.
Every mechanism must be stopped cleanly before module unload, or the kernel crashes.
