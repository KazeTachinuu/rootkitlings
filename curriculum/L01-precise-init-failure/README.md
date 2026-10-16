# L01: Goto Cleanup

Your module loads cleanly, but it allocates nothing.
A real module claims resources during init: memory, devices, proc entries.
If the third allocation fails, the first two must be freed.
Skip cleanup and you leak kernel memory.
Free something never allocated and you corrupt the heap.

The kernel convention is a goto cleanup chain: each failure label undoes
everything allocated before it, in reverse order.

## The pattern

```c
static int __init rk_init(void)
{
	chan_a = rk_channel_create(1);
	if (IS_ERR(chan_a))
		return PTR_ERR(chan_a);    // nothing to clean up

	chan_b = rk_channel_create(2);
	if (IS_ERR(chan_b))
		goto free_a;              // chan_a exists, free it

	return 0;

free_a:
	kfree(chan_a);
	return PTR_ERR(chan_b);
}
```

Labels stack in reverse allocation order.
Each label frees one resource and falls through to the labels below it.
Exit does the same teardown in reverse:

```c
static void __exit rk_exit(void)
{
	kfree(chan_b);    // last allocated, first freed
	kfree(chan_a);
}
```

## Error pointers

Kernel allocators cannot return both a pointer and an error code.
The convention is to encode errors as invalid pointers in the top of the address
space:

```c
void *ERR_PTR(long error);     // encode: ERR_PTR(-ENOMEM)
long PTR_ERR(const void *ptr); // decode: returns the negative errno
bool IS_ERR(const void *ptr);  // test: true if ptr is an error
```

`kzalloc` returns NULL on failure, so your `rk_channel_create` converts that to
`ERR_PTR(-ENOMEM)`.
Callers use `IS_ERR` to detect failure and `PTR_ERR` to extract the error code.

## Injecting failures

The build system defines `RKLINGS_FAIL_POINT` at compile time.
Your code checks this value to simulate allocation failures at specific points,
letting the validator prove that each goto path works:

```c
enum rk_fail_point {
	RK_FAIL_NONE         = 0,   // both succeed
	RK_FAIL_FIRST_ALLOC  = 1,   // first alloc fails
	RK_FAIL_SECOND_ALLOC = 2,   // second alloc fails
};
```

## The trap

If the second allocation fails but you free `chan_b` (which was never
allocated), you corrupt the heap.
The goto chain must free only what was successfully created.
That is the entire point of having separate labels.

## You write

`core/main.c` (~80 lines):

- `rk_channel_create`
- `rk_init` with goto chain
- `rk_exit` with reverse teardown

## API

```c
struct rk_channel *rk_channel_create(unsigned int id);

void *kzalloc(size_t size, gfp_t flags);
void kfree(const void *ptr);
void *ERR_PTR(long error);
long PTR_ERR(const void *ptr);
bool IS_ERR(const void *ptr);
```

## Test it

```
$ rklings run l01
root@vm:~# insmod rklings.ko
root@vm:~# grep rklings /proc/modules
rklings 12288 0 - Live 0x...
root@vm:~# rmmod rklings

root@vm:~# insmod rklings_fail1.ko
insmod: ERROR: could not insert module: Operation canceled
root@vm:~# grep rklings /proc/modules
(no output, module rejected)

root@vm:~# insmod rklings_fail2.ko
insmod: ERROR: could not insert module: Operation canceled
root@vm:~# grep rklings /proc/modules
(no output, first alloc freed via goto)
```

## Validate

```
$ rklings validate l01
  [+] rklings.ko builds at all three fail points
  [+] FAIL_POINT=0: loads, both channels created
  [+] FAIL_POINT=1: rejects load, no module resident
  [+] FAIL_POINT=2: rejects load, no module resident
  [+] rmmod after FAIL_POINT=0 succeeds cleanly
  PASS
```

## Next

The cleanup chain works, but the module owns no persistent data.
L02 adds kmalloc/kfree with an opaque struct and module_param so the module
carries state across calls.
