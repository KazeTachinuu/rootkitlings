# React to a mkdir from the kernel

Your module has published state and accepted reads through `/proc`, but it has
never reacted to anything on the real filesystem.
This changes now: the module can intercept filesystem events.

## The hook header

You will include `rklings_hook.h`, a black-box header that wires up mkdir
interception for you.
You do not need to read or understand its internals.
It provides three things:

1. `RK_ON_MKDIR(name)` -- a macro to define your callback function.
   `name` is the basename of the directory the user tried to create.
2. `rk_hook_start()` -- call this from `module_init` to activate the hook.
3. `rk_hook_stop()` -- call this from `module_exit` to deactivate it.

Your callback returns a negative errno (like `-ENOENT`) to block the mkdir, or
`0` to allow it.

## What to build

Write a callback that:
- Checks if `name` starts with `"rklings."`
- If it matches, prints the part after the prefix to dmesg
- Returns `-ENOENT` to prevent the actual directory from being created
- If it does not match, returns `-ENOENT` (let the hook header handle it)

## Demo

```
$ sudo insmod rklings.ko
$ mkdir rklings.hello
mkdir: cannot create directory 'rklings.hello': No such file or directory
$ dmesg | tail -1
rklings: intercepted mkdir: hello
$ mkdir somethingelse
(no dmesg output from your module)
```

## Verify

```
./rklings check
```

The test loads your module, runs `mkdir rklings.hello`, and checks that `dmesg`
contains the intercepted message.
It also verifies that a non-matching mkdir does not trigger your callback's
print.
