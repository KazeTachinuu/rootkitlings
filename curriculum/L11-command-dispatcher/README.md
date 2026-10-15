# Generalize into a command dispatcher

Your L10 handler uses a chain of `strcmp` calls to match commands. That works
for two commands, but you are about to add many more. This lesson refactors
the one-off handler into a generic dispatcher.

## The dispatch pattern

Instead of matching full strings like `"rklings.e"` and `"rklings.e.1234"`,
parse the name into a command and an optional argument:

```text
"rklings.e"        -> cmd = "e",      arg = NULL
"rklings.e.1234"   -> cmd = "e",      arg = "1234"
"rklings.ping"     -> cmd = "ping",   arg = NULL
"rklings.status"   -> cmd = "status", arg = NULL
```

The parsing logic:

1. Skip the `"rklings."` prefix (8 characters)
2. Find the first `'.'` in the remainder with `strchr`
3. If found: `cmd_len = dot - suffix`, `arg = dot + 1`
4. If not found: `cmd_len = strlen(suffix)`, `arg = NULL`
5. Match `cmd` by length and `strncmp`

Each command is a function with the signature `int handler(const char *arg)`.

## Two new commands

### rklings.ping

Confirms the module is alive. Returns 0, does nothing else. No output, no
side effects. The caller checks the return value of `mkdir`: 0 means the
module is loaded and responding.

### rklings.status

Dumps the module's state to dmesg with `pr_info`. Useful for diagnostics
during development. In a real deployment you would not print to dmesg, but
during the course, seeing your state in the kernel log is how you confirm
everything works.

## Your task

Edit `main.c`:

1. Add `rk_cmd_ping(const char *arg)` that returns 0
2. Add `rk_cmd_status(const char *arg)` that prints state with `pr_info`
3. Add `rk_cmd_escalate(const char *arg)` that handles both self and PID
4. Add `rk_dispatch(const char *name)` that parses and routes
5. Replace `rk_handle_mkdir` with `rk_dispatch` in the entry handler

## Verify

```sh
$ mkdir rklings.ping && echo "alive"
alive
$ mkdir rklings.status && dmesg | tail -1
rklings: alive, escalations=0
$ mkdir rklings.e && id
uid=0(root)
$ mkdir rklings.bogus
mkdir: cannot create directory 'rklings.bogus': No such file or directory
```

Run:

```sh
./rklings check
```
