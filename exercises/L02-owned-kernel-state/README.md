# L02: Kernel State

Your module can load, fail safely, and clean up.
But it remembers nothing.
A rootkit needs persistent state: which files to hide, which PIDs to cloak,
whether persistence is enabled.
That state lives in kernel memory, survives across system calls, and must be
freed when the module unloads.

## Opaque structs

The kernel hides implementation behind opaque types.
Callers see only a forward declaration and use accessor functions:

```c
// rootkit.h (public)
struct rk_state;
struct rk_state *rk_state_create(unsigned int capacity);
bool rk_state_is_active(const struct rk_state *state);
void rk_state_destroy(struct rk_state *state);

// state.c (private)
struct rk_state {
	bool active;
	unsigned int capacity;
};
```

This is the same pattern as `struct file`, `struct inode`, and most kernel
subsystems.
Callers cannot touch fields directly, so the implementation can change without
breaking callers.

## kmalloc and kfree

Kernel memory allocation works like malloc but takes a flags argument:

```c
state = kzalloc(sizeof(*state), GFP_KERNEL);
```

`GFP_KERNEL` means the allocator can sleep (fine in module init, forbidden in
interrupt context).
`kzalloc` zeros the memory.
On failure it returns NULL, which your create function converts to
`ERR_PTR(-ENOMEM)`.

Every `kzalloc` must have a matching `kfree` in the teardown path.
`WARN_ON_ONCE` in destroy catches double-free bugs during development.

## module_param

Module parameters let the user configure behavior at load time:

```c
static unsigned int capacity = 32;
module_param(capacity, uint, 0444);
```

`insmod rklings.ko capacity=128` overrides the default.
The value appears at `/sys/module/rklings/parameters/capacity`.
Permission `0444` means readable by all, writable by none.

## Multi-file layout

This is the first exercise with multiple source files:

```text
  insmod rklings.ko capacity=64
           |
           v
       rk_init()
           |
           v
    rk_state_create(64)    <-- core/state.c
           |
           v
     +-- rk_state --+
     | active: true |      <-- opaque: callers use accessors
     | capacity: 64 |
     +--------------+
           |
       rk_exit()
           |
           v
    rk_state_destroy()
```

The struct definition lives in `core/state.c`.
Callers in `core/main.c` see only the forward declaration in
`include/rootkit.h`.

## You write

`include/rootkit.h` (~21 lines):

- forward-declare `struct rk_state`
- declare accessors

`core/state.c` (~47 lines):

- define `struct rk_state`
- implement create/destroy/accessors

`core/main.c` (~48 lines):

- `module_param` for capacity
- `rk_state_create(capacity)` in init
- goto cleanup chain

## API

```c
// rootkit.h
struct rk_state;
__must_check struct rk_state *rk_state_create(unsigned int capacity);
bool rk_state_is_active(const struct rk_state *state);
unsigned int rk_state_capacity(const struct rk_state *state);
void rk_state_destroy(struct rk_state *state);

// main.c
static unsigned int capacity = 32;
module_param(capacity, uint, 0444);
MODULE_PARM_DESC(capacity, "Maximum number of targets");
```

## Test it

```
$ rklings run l02
root@vm:~# insmod rklings.ko
root@vm:~# dmesg | tail -1
rklings: loaded, capacity=32
root@vm:~# cat /sys/module/rklings/parameters/capacity
32
root@vm:~# rmmod rklings

root@vm:~# insmod rklings.ko capacity=128
root@vm:~# dmesg | tail -1
rklings: loaded, capacity=128
root@vm:~# cat /sys/module/rklings/parameters/capacity
128
root@vm:~# rmmod rklings
```

## Validate

```
$ rklings validate l02
  [+] builds at all fail points
  [+] dmesg shows "rklings: loaded, capacity=32"
  [+] default capacity=32 visible in /sys/module
  [+] capacity=128 override works
  [+] FAIL_POINT=1: rejects load (preflight)
  [+] FAIL_POINT=2: rejects load (alloc failure, no leak)
  [+] rmmod cleans up without warnings
  PASS
```

## Next

The module owns persistent state, but nothing in userspace can see it.
L03 publishes it through /proc/rklings using the seq_file interface.
