# 06 - A callback reacts to parameter writes
## Why this step

Ling 05 proved that a sysfs write changes `name`, but no module code ran at the
moment of that change. Init cannot react because init has already finished.

A setter function runs
whenever it receives a new value.

Keep the `name` variable from ling 05.

## See the function pointer first

These two expressions do different things:

```c
set_name       /* the address of the function */
set_name(...)  /* call the function now */
```

An operations table stores the first form.
Assigning a callback does not execute it.

## Replace the simple parameter registration

Replace `module_param(name, charp, 0644)` with:

```c
static int set_name(const char *value, const struct kernel_param *parameter)
{
	int error;

	error = param_set_charp(value, parameter);
	if (error)
		return error;

	/* react to the change here */
	return 0;
}

static const struct kernel_param_ops name_ops = {
	.set = set_name,
	.get = param_get_charp,
};

module_param_cb(name, &name_ops, &name, 0644);
```

Keep `MODULE_PARM_DESC` after this block.

The parameter system calls `.set` on a load argument or sysfs write
and `.get` on a sysfs read:

```
                    kernel_param_ops
                   +----------------+
                   | .set = set_name|
                   | .get = get_name|
                   +-------+--------+
                           |
         +-----------------+-----------------+
         |                                   |
 insmod name=phantom             cat /sys/.../name
 printf acu > /sys/.../name
         |                                   |
         v                                   v
     .set(value)                        .get(buf)
         |                                   |
         v                                   v
   param_set_charp          param_get_charp
   stores value                reads *name
         |
         v
   your custom reaction
   (log, counter, etc.)
```

Load-time ordering with `insmod hello.ko name=phantom`:

```
insmod hello.ko name=phantom
  |
  v
parse "name=phantom" --> .set(phantom) --> your reaction runs
  |
  v
your init()        --> pr_info("loaded name=phantom")
  |
  v
module is loaded, sysfs file appears
```

## Contract

The check script verifies:

- Loading with `name=phantom` sets the parameter to `phantom`
- Writing `specter` to the sysfs file updates the parameter
- The setter callback fires on each change (your reaction runs)

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
cd /root
insmod hello.ko name=phantom
cat /sys/module/hello/parameters/name
printf '%s' specter > /sys/module/hello/parameters/name
cat /sys/module/hello/parameters/name
dmesg | tail
rmmod hello
```

## Verify

```bash
./rootkitlings check 6
```
> **What a defender sees**: anyone with write access to the sysfs file triggers the callback.
> There is no authentication, no caller check, no rate limit. Ling 24 introduces `current`.

See also: [07 - Allocation](../07-allocation/README.md).
