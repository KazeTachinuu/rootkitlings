# L02: Remember Module State

## Mission

The module will eventually track hidden files, PIDs, and ports. It starts by
learning to hold private state.

L01 showed how to clean up when loading fails. Now the module needs to remember
information for as long as it stays loaded.

Create one private state object, remember whether the module is active, and free
the object when the module unloads.

## What you will observe

Load the module once active, then once inactive:

```text
load command                 observed result
-------------------------    ----------------------------------
insmod rklings.ko            loaded with active=1
insmod rklings.ko active=0   loaded with active=0
injected allocation failure  ENOMEM; module remains absent
```

`active=0` is a load-time setting passed to `insmod`.
The supplied declaration makes it available as the boolean variable `active`.

The module remembers the chosen value until it unloads.

## Workspace

```text
exercises/L02-module-state
├── core
│   ├── main.c
│   └── state.c
├── include
│   └── rootkit.h
└── Kbuild
```

Edit two files:

- `core/main.c` creates and destroys the state.
- `core/state.c` stores whether the module is active.

The header and `Kbuild` are supplied.

## Let only `state.c` see the fields

The header names `struct rk_state` without listing its fields:

```c
struct rk_state;

__must_check struct rk_state *rk_state_create(bool active);
bool rk_state_is_active(const struct rk_state *state);
void rk_state_destroy(struct rk_state *state);
```

This is called an opaque type: `main.c` knows that `struct rk_state` exists but
cannot access its fields directly.

```text
core/main.c                         core/state.c

struct rk_state *state             struct rk_state {
        │                               bool active;
        │                           };
        |
        ├── rk_state_create() --------> allocate and initialize
        ├── rk_state_is_active() -----> read the stored value
        └── rk_state_destroy() -------> free the allocation
```

`rk_state_create` may fail.
The supplied `__must_check` annotation warns if code ignores its result.
Handle its error pointer exactly as you did in L01.

The supplied `rk_state_alloc` function reserves kernel memory for the object.
`kfree` gives that memory back:

```text
rk_state_alloc(sizeof(*state))  ->  state pointer  ->  kfree(state)
```

## TODOs

```text
TODO 1  core/state.c:16:1  define the private active field
TODO 2  core/state.c:20:2  allocate state and remember the active value
TODO 3  core/state.c:26:2  return the stored active value
TODO 4  core/state.c:32:2  free the state and report its removal
TODO 5  core/main.c:24:2   create state or reject the load
TODO 6  core/main.c:31:2   destroy state during unload
```

### TODO 1 -- `core/state.c:16:1` -- Define the private state

Define `struct rk_state` with one `bool active` field.
Keeping the definition in `state.c` prevents `main.c` from reading or changing
the field directly.

### TODO 2 -- `core/state.c:20:2` -- Create initialized state

```text
allocation succeeds  initialized state pointer
allocation fails     ERR_PTR(-ENOMEM)
```

1. Declare a state pointer.
2. Call the supplied `rk_state_alloc(sizeof(*state))` helper.
3. Return `ERR_PTR(-ENOMEM)` if it returns `NULL`.
4. Store the `active` argument in the new state and return its pointer.

`sizeof(*state)` stays correct if the structure changes.

### TODO 3 -- `core/state.c:26:2` -- Read the stored active value

Return `state->active`.
This function lets `main.c` read the value without accessing the field directly.

### TODO 4 -- `core/state.c:32:2` -- Destroy the state

1. Release the allocation with `kfree(state)`.
2. Log `rklings: released state\n`.

After `kfree`, no code may access the object through that pointer.

### TODO 5 -- `core/main.c:24:2` -- Create state during load

```text
state pointer  -> keep it, log active, return 0
error pointer  -> clear state, return the stored error
```

1. Store `rk_state_create(active)` in the global `state` pointer.
2. If `IS_ERR(state)` is true, save `PTR_ERR(state)`, clear `state`, and return
   the saved error.
3. On success, log `rklings: loaded, active=%u\n` using
   `rk_state_is_active(state) ? 1U : 0U`, then return `0`.

Log success only after the state exists.

### TODO 6 -- `core/main.c:31:2` -- Destroy state during unload

1. Log `rklings: unloaded\n`.
2. Call `rk_state_destroy(state)`.
3. Set `state` to `NULL`.

After the pointer is cleared, the module no longer refers to the freed object.

## Test it

From the repository root, build the module and enter the VM:

```console
$ ./rklings run L02
```

Inside the VM, try both starting states:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# dmesg | tail -1
rklings: loaded, active=1
root@victim:~# cat /sys/module/rklings/parameters/active
Y
root@victim:~# rmmod rklings

root@victim:~# insmod /rklings/rklings.ko active=0
root@victim:~# dmesg | tail -1
rklings: loaded, active=0
root@victim:~# rmmod rklings
```

## Validate


```console
$ ./rklings validate L02
  [+] Normal state lifetime
  [+] State allocation rejection
  PASS
```


## Next

The module now remembers private state, but the shell cannot read it.
L03 publishes a read-only `/proc/rklings` entry and ties that entry's lifetime
to the state it reads.
