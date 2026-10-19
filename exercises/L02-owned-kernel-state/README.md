# L02: Give the Module Private State

L01 allocated temporary resources during initialization.
This exercise turns that pattern into a small state component owned for the
module's entire loaded lifetime.

The state starts active, remembers a configurable target capacity, and is
destroyed when the module unloads.

## What you will observe

Load the same module with two different capacities:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# cat /sys/module/rklings/parameters/capacity
32
root@victim:~# rmmod rklings

root@victim:~# insmod /rklings/rklings.ko capacity=128
root@victim:~# cat /sys/module/rklings/parameters/capacity
128
root@victim:~# rmmod rklings
```

The parameter is visible in sysfs, while the allocated state remains private
inside the module.

## Workspace

This is the first multi-file module:

```text
exercises/L02-owned-kernel-state
├── Kbuild
├── core
│   ├── main.c
│   └── state.c
└── include
    └── rootkit.h
```

Read the component and lifetime sections below before completing the TODOs.
`Kbuild` is already configured to compile the files together.

## Separate the interface from the data

`core/main.c` needs to create and destroy state, but it does not need to know
how that state is represented.
The header therefore declares the type without defining its fields:

```c
/* include/rootkit.h */
struct rk_state;

struct rk_state *rk_state_create(unsigned int capacity);
bool rk_state_is_active(const struct rk_state *state);
unsigned int rk_state_capacity(const struct rk_state *state);
void rk_state_destroy(struct rk_state *state);
```

This is an opaque type:

```text
core/main.c                           core/state.c

struct rk_state *state               struct rk_state {
        |                                 bool active;
        |                                 unsigned int capacity;
        |                             };
        |
        +-- rk_state_create(32) ---------------> allocate and initialize
        +-- rk_state_capacity(state) ----------> read a field safely
        +-- rk_state_destroy(state) -----------> invalidate and free

can hold the pointer                   owns the layout
cannot access state->capacity          may change the layout later
```

Keeping the fields private prevents other components from bypassing the state
component's lifetime rules.

## Allocate kernel memory

Userspace programs use `malloc` and `free` from the C library.
Kernel code has no C library; this exercise uses `kzalloc` and `kfree`:

```c
state = kzalloc(sizeof(*state), GFP_KERNEL);
if (!state)
	return ERR_PTR(-ENOMEM);
```

`kzalloc` allocates kernel memory and fills it with zeroes. `sizeof(*state)`
follows the pointed-to type and remains correct if the variable name changes.

`GFP_KERNEL` describes the allocation context.
It permits the allocator to sleep while memory is reclaimed, which is valid
during module initialization.

Initialize both fields only after allocation succeeds:

```c
state->active = true;
state->capacity = capacity;
```

`rk_state_destroy` marks the state inactive before calling `kfree`.
The supplied `WARN_ON_ONCE` guard makes an invalid second destruction visible
during development.

## Pass configuration at load time

A module parameter turns a C variable into an `insmod` option and a sysfs file:

```c
static unsigned int capacity = 32;

module_param(capacity, uint, 0444);
MODULE_PARM_DESC(capacity, "Maximum number of targets");
```

```text
insmod rklings.ko capacity=128
                       |
                       v
             main.c: capacity == 128
                       |
                       v
             rk_state_create(128)
                       |
                       v
 /sys/module/rklings/parameters/capacity contains "128"
```

`uint` defines the parameter type.
Mode `0444` makes the sysfs value readable but not writable after load.

## Own the lifetime in `main.c`

The module owns exactly one state pointer:

```text
LOAD                              UNLOAD
rk_init                           rk_exit
  |                                 |
  +-- preflight check               +-- rk_state_destroy(state)
  +-- rk_state_create(capacity)     +-- state = NULL
  +-- save state pointer            +-- return
  +-- return 0

If creation fails:
  rk_state_create(capacity)
      +-- error pointer
              +-- extract errno
              +-- state = NULL
              +-- return errno
```

If creation returns an error pointer, extract the error, set the global pointer
back to `NULL`, and reject the load.
Do not leave an encoded error in a variable that normally means “owned state.”

## Implement it

In `include/rootkit.h`:

1. Forward-declare `struct rk_state`.
2. Declare create, destroy, active, and capacity operations.
3. Mark creation `__must_check` so callers cannot ignore failure.

In `core/state.c`:

1. Define the private structure.
2. Allocate it with `kzalloc`.
3. Return `ERR_PTR(-ENOMEM)` on injected or real allocation failure.
4. Initialize both fields and implement both accessors.
5. Invalidate and free the state in `rk_state_destroy`.

In `core/main.c`:

1. Declare the read-only `capacity` parameter with default value 32.
2. Preserve the supplied preflight rejection.
3. Create the state and propagate its exact error.
4. Log the selected capacity only after success.
5. Destroy the state and clear the pointer during exit.

## Test it

After completing the implementation, build it and enter the VM:

```console
$ ./rklings run L02
```

Inside the VM, test both the default and an override:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# dmesg | tail -1
rklings: loaded, capacity=32
root@victim:~# cat /sys/module/rklings/parameters/capacity
32
root@victim:~# rmmod rklings

root@victim:~# insmod /rklings/rklings.ko capacity=128
root@victim:~# dmesg | tail -1
rklings: loaded, capacity=128
root@victim:~# rmmod rklings
```

## Validate

From the repository root:

```console
$ ./rklings validate L02
```

The validator checks component boundaries, the state accessors, both capacity
values, precise `ECANCELED` and `ENOMEM` failures, repeated failed loads, clean
unload, and rejection of an unknown failure point.

## Next

The module owns state, but a shell cannot inspect that state directly.
L03 publishes a read-only `/proc/rklings` view whose lifetime is tied to the
state it reads.
