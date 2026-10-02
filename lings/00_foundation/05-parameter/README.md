# 05 - Parameters are live module state
## Why this step

Our module can load safely, but changing its greeting still requires editing C
and rebuilding. We need a value userspace can provide at load time and inspect
or change while the module is loaded.

Start from the successful module at the end of ling 04.

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

Use the parameter in your init and exit log calls so you can see
the value changing.

## Contract

The check script verifies:

- Module loads with default parameter (`name` == `world`)
- Module loads with `name=phantom` (parameter is `phantom`)
- Parameter can be changed at runtime via sysfs
- Module unloads cleanly

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
cd /root
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
./rootkitlings check 5
```

The check loads with no arg (default `world`), then with `name=phantom`,
then writes `specter` via sysfs. It verifies the parameter value at each step
and checks that exit prints the last written value.
> **What a defender sees**: parameters are world-readable in `/sys/module/hello/parameters/`.
> Anyone on the system can read them. And writing triggers no module code at all.

See also: [06 - Parameter callback](../06-parameter-callback/README.md).
