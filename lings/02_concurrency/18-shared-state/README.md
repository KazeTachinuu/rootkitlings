# 18 - Shared state

## Why this step

Two threads writing to the same variable without synchronization.
This ling deliberately demonstrates a data race so you can see lost
updates before ling 19 shows how to fix them.

```
  thread A                thread B
  --------                --------
  read counter (500)      read counter (500)
  add 1       (501)       add 1       (501)
  write back  (501)       write back  (501)

  Result: 501. Should be 502. One update lost.
```

On a single CPU, the scheduler can preempt between read and write.
On multiple CPUs, both threads run the read at the same instant.

## What you already know

From ling 16: `kthread_run`, `kthread_should_stop`. This ling uses the
same thread creation pattern but spawns two threads that share a counter.

## Build it

Create a module that:

1. Spawns two kthreads with `kthread_run` (you only need `linux/kthread.h`).
2. Both increment a shared `unsigned long` counter 10,000 times each.
3. When both threads finish, log the final count vs. the expected 20,000.
4. `/proc/rk_race` returns the final count.

You need a way to know when both threads are done. A simple approach:
each thread increments a `done_count` when it finishes its loop. When
`done_count` reaches 2, log the result.

The threads run a tight loop (no sleeping), so they complete quickly.
Read `/proc/rk_race` after a short delay.

## Predict

- Will the final count always be exactly the same on repeated runs?
- Is the race guaranteed to happen?
- What determines how many updates are lost?

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
sleep 2
cat /proc/rk_race       # likely less than 20000
dmesg | tail
rmmod hello
```


## Contract

The check script verifies:

- `/proc/rk_race` exists after loading.
- After 2 seconds, counter is positive (threads ran).
- On multi-CPU, the count will likely be less than 20000 (demonstrating the race).
- On single-vCPU, 20000 is also accepted (scheduler may serialize threads).
- `/proc/rk_race` disappears after unload.

## Verify

```bash
./rootkitlings check 18
```

> **What a defender sees**: nothing directly. But data races in rootkit
> code cause intermittent crashes that leave debug traces. A stable
> rootkit protects its shared state.

See also: [19 - Mutex](../19-mutex/README.md).
