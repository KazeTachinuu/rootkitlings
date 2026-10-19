# L01: Abort a Partial Initialization Cleanly

L00 handled the successful module lifecycle.
This exercise handles the harder case: initialization acquires one resource,
fails while acquiring the next, and must return the kernel to its original
state.

You will allocate two small channels and use a cleanup chain that is correct at
every possible stopping point.

## What can go wrong

Suppose initialization performs these steps:

```text
start
  |
  +-- create channel A -- success -- A is owned
  |
  +-- create channel B -- failure -- what happens to A?
```

Returning immediately leaks channel A.
Freeing channel B is also wrong because channel B was never created.

The correct cleanup depends on how far initialization progressed:

```text
failure point       resources owned       required cleanup
-------------       ---------------       ----------------
before A            none                  return error
while creating A    none                  return error
while creating B    A                     free A, return error
no failure          A and B               keep both until module exit
```

This ownership table is the reason for the separate cleanup labels.

## Workspace

The workspace already defines `struct rk_channel`, the allocation helper, and
three compile-time failure points.
Your work is in `core/main.c`.
Read the mechanism below before completing its two TODOs.

The runner will stage the normal build as `/rklings/rklings.ko`.
Validation builds the failure variants automatically, so you do not maintain
separate source files.

## Read kernel error pointers

`rk_channel_create` must return either a channel pointer or a precise error.
Kernel code commonly encodes a small negative error number inside a pointer:

```text
successful result                     failed result

rk_channel_create()                   rk_channel_create()
        |                                      |
        v                                      v
 valid struct rk_channel *              ERR_PTR(-ENOMEM)
        |                                      |
   IS_ERR(ptr) == false                   IS_ERR(ptr) == true
                                               |
                                               v
                                        PTR_ERR(ptr)
                                               |
                                               v
                                           -ENOMEM
```

The three helpers have separate jobs:

```c
IS_ERR(pointer)   /* did this pointer-shaped result contain an error? */
PTR_ERR(pointer)  /* recover its negative errno */
ERR_PTR(-ENOMEM)  /* encode an errno as a pointer-shaped result */
```

Do not test an error pointer with only `if (!pointer)`. `ERR_PTR(-ENOMEM)` is
not `NULL`.

## Build the cleanup chain

Acquire resources from top to bottom and release them from bottom to top:

```c
chan_a = rk_channel_create(1);
if (IS_ERR(chan_a))
	return PTR_ERR(chan_a);

chan_b = rk_channel_create(2);
if (IS_ERR(chan_b)) {
	ret = PTR_ERR(chan_b);
	goto free_a;
}

return 0;

free_a:
	kfree(chan_a);
	return ret;
```

The important invariant is simple:

```text
execution can reach free_a only after channel A became owned
```

Save the error from channel B before cleanup.
The module loader must receive the error that caused initialization to fail, not
an unrelated cleanup result.

Normal module exit owns both channels, so it releases B first and A second:

```c
static void __exit rk_exit(void)
{
	kfree(chan_b);
	kfree(chan_a);
}
```

Reverse order matters once resources depend on one another.
Later lessons use exactly this pattern for state, proc entries, and probes.

## How failure injection works

Kbuild passes `RKLINGS_FAIL_POINT` to the C compiler with `-D`:

```make
RKLINGS_FAIL_POINT ?= 0
ccflags-y += -DRKLINGS_FAIL_POINT=$(RKLINGS_FAIL_POINT)
```

The selected number becomes a constant in that compiled module:

```c
enum rk_fail_point {
	RK_FAIL_NONE = 0,
	RK_FAIL_FIRST_ALLOC = 1,
	RK_FAIL_SECOND_ALLOC = 2,
};
```

Use the constant at each allocation site:

```c
if (RKLINGS_FAIL_POINT == RK_FAIL_FIRST_ALLOC)
	chan_a = ERR_PTR(-ENOMEM);
else
	chan_a = rk_channel_create(1);
```

Building with `RKLINGS_FAIL_POINT=0`, `1`, or `2` therefore produces three
different `rklings.ko` files:

```text
value 0   both allocation calls run
value 1   channel A receives ERR_PTR(-ENOMEM)
value 2   channel A is created, channel B receives ERR_PTR(-ENOMEM)
```

The supplied preprocessor guard accepts only those three values:

```c
#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 2
#error "unknown RKLINGS_FAIL_POINT"
#endif
```

## Implement it

Complete the two TODOs in `core/main.c`:

1. Create channel A.
2. Return its exact error if creation fails.
3. Create channel B.
4. Save B's exact error and release A if B fails.
5. Log success only after both channels exist.
6. On normal exit, release B and then A.

Do not free a failed error pointer and do not call exit cleanup from a failed
`rk_init`.
The kernel never calls `rk_exit` when `rk_init` returns an error.

## Test it

Build the normal variant and enter the VM:

```console
$ ./rklings run L01
```

Test the successful build inside the VM:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# dmesg | tail -1
rklings: loaded
root@victim:~# rmmod rklings
```

The important manual result is a clean load and unload.
The automated validator rebuilds the same source with failure point 1 and
confirms that repeated load attempts return `ENOMEM` without leaving the module
resident:

```text
failed rk_init()
      |
      +-- /sys/module/rklings does not exist
      +-- /proc/modules has no rklings entry
```

## Validate

From the repository root:

```console
$ ./rklings validate L01
```

The validator checks the inherited Kbuild and metadata, the normal lifecycle,
precise `ENOMEM` propagation, repeated failed loads, and rejection of an unknown
failure point.

## Next

Initialization can now acquire resources without leaking them on failure.
L02 turns the first owned resource into module state that remains available
until unload.
