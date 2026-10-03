# Concurrency

Defer work, run background threads, and protect shared state.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 17 | Timer | `timer_setup` + `mod_timer` for delayed work. | A timer still firing during rmmod crashes the kernel. |
| 18 | Workqueue | Queue deferred work, cancel before unload. | Unfinished work after `cancel_work_sync` is a use-after-free. |
| 19 | Kernel thread | `kthread_create`, `kthread_stop`, join. | A thread that ignores `kthread_should_stop` blocks rmmod forever. |
| 20 | Waiting | `wait_event_interruptible` + `wake_up`. | Periodic sleeps add needless wakeups and response latency. |
| 21 | Completion | Signal that finite worker code has finished. | A guessed delay cannot prove that teardown is safe. |
| 22 | Mutex | `mutex_lock`/`mutex_unlock` around the shared counter. | Mutex in interrupt context deadlocks the kernel. |
| 23 | Atomic | `atomic_t` for a single transition flag. | Atomics protect one variable, not a multi-field invariant. |

Timers and workqueues power beaconing (periodic callback to the controller).
Kthreads run the beacon loop. Mutexes protect the task queue.
Stop every mechanism before module unload. A callback into unloaded module text
can crash the kernel.

After ling 23: build [Capstone 1: Beacon](../../capstones/1-beacon/README.md).
