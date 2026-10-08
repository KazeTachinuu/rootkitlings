# 06 - A callback reacts to parameter writes

## Run code when a parameter changes

Ling 05 proved that a sysfs write changes `name`, but no module code ran at the moment of that change.
Init cannot react because init has already finished.

A parameter callback can validate a proposed value and react after storing it.
Here, every successful update increments a counter that userspace can read:

```text
printf specter > .../name
            |
            v
name_param_set() --> param_set_charp() --> changes++
            |
            v
name=specter          changes=2
```

Keep the `name` variable from ling 05.
Add a counter and expose it as a read-only parameter:

```c
static unsigned int changes;
module_param(changes, uint, 0444);
```

## See the function pointer first

These two expressions do different things:

```c
name_param_set       /* the address of the function */
name_param_set(...)  /* call the function now */
```

An operations table stores the first form.
Assigning a callback does not execute it.

## Replace the simple parameter registration

Replace `module_param(name, charp, 0644)` with:

```c
static int name_param_set(const char *value,
			  const struct kernel_param *parameter)
{
	int error;

	error = param_set_charp(value, parameter);
	if (error)
		return error;

	/* react to the change here */
	return 0;
}

static const struct kernel_param_ops name_param_ops = {
	.set = name_param_set,
	.get = param_get_charp,
};

module_param_cb(name, &name_param_ops, &name, 0644);
```

Keep `MODULE_PARM_DESC` after this block.
Increment `changes` only after `param_set_charp()` succeeds.

The parameter system calls `.set` on a load argument or sysfs write and `.get` on a sysfs read:

```text
module_param_cb
       |
       v
kernel_param_ops
       |
       +-- .set = name_param_set
       |          |
       |          +-- validate and store name
       |          +-- increment changes
       |
       +-- .get = param_get_charp
                  |
                  +-- return the current name
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

## Observable behavior

The check script verifies:

- Loading with `name=phantom` sets the parameter to `phantom`
- Writing `specter` to the sysfs file updates the parameter
- The setter callback fires on each change (your reaction runs)

## Try it

```bash
./rootkitlings run L06
```

Inside the VM:

```sh
insmod hello.ko name=phantom
cat /sys/module/hello/parameters/name
printf '%s' specter > /sys/module/hello/parameters/name
cat /sys/module/hello/parameters/name
dmesg | tail
rmmod hello
```

## Verify

```bash
./rootkitlings check L06
```

## Limits and detection

Every permitted sysfs write invokes the setter in the writer's process context.
This interface has no caller authentication or rate limit.
Ling 24 introduces the `current` task pointer used to inspect the caller.

See also: [07 - Allocation](../07-allocation/README.md).
