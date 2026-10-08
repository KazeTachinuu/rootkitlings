# 05 - Parameters are live module state

## Configure a module without rebuilding it

A module parameter connects one kernel variable to load-time arguments and a sysfs file.
For example, the same module can load with a default target or an operator-selected target:

```text
insmod hello.ko name=phantom
                     |
                     v
        /sys/module/hello/parameters/name
                     |
                     v
              name == "phantom"
```

Start from the module used in lesson 04.
Restore init's return value to `0` so the module can load before adding the parameter.

## Add one string parameter

Add the header that declares the parameter interface:

```c
#include <linux/moduleparam.h>
```

Then add this file-scope state after the includes:

```c
static char *name = "world";
module_param(name, charp, 0644);
MODULE_PARM_DESC(name, "Name used by the greeting");
```

`static` limits this variable's visibility to the current file.
`charp` selects the string parameter parser.
`0644` defines the sysfs permissions:

| Owner | Group | Other |
|---|---|---|
| `rw-` (`6`) | `r--` (`4`) | `r--` (`4`) |

Sysfs is a kernel-provided filesystem mounted at `/sys`.
It exposes the parameter as a file-like interface to the live variable:

```
 userspace                         kernel
+---------------------------+     +------------------+
|                           |     |  static char     |
| insmod name=phantom         |---->|  *name = "phantom" |
|                           |     |                  |
| cat /sys/.../name  <------+-----+-  read variable  |
|                           |     |                  |
| printf acu > /sys/.../    |---->|  *name = "acu"   |
|                           |     |                  |
| rmmod hello               |     |  exit sees       |
|                           |     |  "acu"           |
+---------------------------+     +------------------+

module_param wires:  variable <---> sysfs file
one backing store, two access paths
```

Use the parameter in your init and exit log calls so you can see the value changing.

## Observable behavior

The check script verifies:

- Loading without an argument exposes `world`.
- Loading with `name=phantom` exposes `phantom`.
- Writing `specter` through sysfs changes the same kernel variable.
- Unloading removes the parameter path.

## Try it

```bash
./rootkitlings run L05
```

Inside the VM:

```sh
insmod hello.ko
cat /sys/module/hello/parameters/name
rmmod hello
insmod hello.ko name=phantom
cat /sys/module/hello/parameters/name
printf '%s' specter > /sys/module/hello/parameters/name
cat /sys/module/hello/parameters/name
dmesg | tail
rmmod hello
dmesg | tail
```

## Verify

```bash
./rootkitlings check L05
```

The check reads the real sysfs parameter after each operation.

## Limits and detection

Mode `0644` makes the parameter world-readable and writable by root.
The parameter name and current value are visible under `/sys/module/hello/parameters/`.
A plain `module_param()` stores a new value but provides no custom validation or side effect.

See also: [06 - Parameter callback](../06-parameter-callback/README.md).
