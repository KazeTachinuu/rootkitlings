# L05: Mkdir Reaction

Your module receives commands through `/proc/rklings`.
That works, but proc entries are visible to any defender who looks.
A rootkit needs a control channel that leaves no trace in the filesystem
listing.

## Why mkdir

`mkdir` is the ideal covert channel.
Any user can call it.
It works from any shell, any script, any programming language.
The directory name carries the command.
And if the module intercepts the call before it reaches the filesystem, no
directory is ever created: there is nothing to find.

```text
  student@vm:~$ mkdir rklings.hello

  userspace                          kernel module
    |                                     |
    |  mkdir("rklings.hello")             |
    |  ------- mkdir("rklings.hello") -->  |
    |                              RK_ON_MKDIR(name)
    |                                name == "rklings.hello"
    |                                strncmp "rklings." -> match
    |                                pr_info("intercepted: hello")
    |                                return -ENOENT
    |  <-- ENOENT (no dir created)         |
```

The mkdir fails with ENOENT, which is normal for a nonexistent path.
Nothing in the filesystem changed.
Nothing in any log except dmesg (which only root can read).

## The black box

`rklings_hook.h` is a header provided in the starter.
It handles the hard part (intercepting the syscall) and exposes three things:

```c
RK_ON_MKDIR(name) { ... }    // define your callback
int rk_hook_start(void);      // register the hook (call from init)
void rk_hook_stop(void);      // unregister (call from exit)
```

Your callback receives `name` as the basename of the mkdir path (no leading
directory).
Return a negative errno to block the mkdir, or 0 to let it through.

L08 will open this header and rebuild it from scratch.
For now, use it as-is.

## You write

`main.c` (~30 lines):

- `RK_ON_MKDIR` callback
- `rk_init` with `rk_hook_start`
- `rk_exit` with `rk_hook_stop`

`rklings_hook.h` is provided.
Do not modify it.

## API

```c
#include "rklings_hook.h"

RK_ON_MKDIR(name) { ... }
int rk_hook_start(void);
void rk_hook_stop(void);

int strncmp(const char *s1, const char *s2, size_t n);
void pr_info(const char *fmt, ...);
```

## Test it

```
$ rklings run l05
root@vm:~# mkdir rklings.hello
mkdir: cannot create directory 'rklings.hello': No such file or directory
root@vm:~# dmesg | tail -1
rklings: intercepted mkdir: hello
root@vm:~# mkdir somethingelse
root@vm:~# ls somethingelse
(directory created normally, no dmesg from module)
```

## Validate

```
$ rklings validate l05
  [+] module loads
  [+] mkdir rklings.hello triggers dmesg
  [+] non-matching mkdir does not trigger
  PASS
```

## Next

The module intercepts mkdir and logs.
L06 replaces logging with action: give the calling process root credentials.
