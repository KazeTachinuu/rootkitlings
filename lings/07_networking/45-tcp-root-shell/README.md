# 45 - Attach a root shell to a TCP socket

An interactive shell already understands commands, quoting, pipelines, and terminal-style input.

Instead of inventing a command protocol, this module attaches an accepted TCP socket to the shell's standard input, output, and error descriptors.

## Observe it

Load the module, then connect from another VM shell:

```sh
insmod hello.ko
nc 127.0.0.1 31337
```

```text
/ # id
uid=0(root) gid=0(root)
/ # pwd
/
/ # exit
```

Usermode helpers start with root credentials, so the connected shell is root.

The listener binds only to loopback.

An unauthenticated shell exposed beyond the disposable VM would be immediately unsafe.

## Descriptor handoff

```text
TCP client -> accepted socket file
                    |
           +--------+--------+
           |        |        |
           v        v        v
         fd 0     fd 1     fd 2
         stdin    stdout    stderr
           \        |        /
            +--- /bin/sh -i -+
```

`sock_alloc_file()` converts the accepted socket to a file reference.

`call_usermodehelper_setup()` prepares the child and invokes `prepare_stdio()` in its context before execution.

`get_unused_fd_flags()` reserves a descriptor and `fd_install()` transfers one file reference into that slot.

Descriptors 0, 1, and 2 must be installed in order.

The first two installations need additional references because each descriptor owns one.

The cleanup callback uses `descriptors_installed` to determine whether it still owns the original reference.

## Implement it

Complete `prepare_stdio()`.

For descriptors 0 through 2, add a file reference before reuse except for the final descriptor, reserve the next descriptor, require the expected number, and install the socket file.

Set `descriptors_installed` only after all three installations succeed.

The listener, accept loop, helper setup, socket shutdown, thread join, and rollback paths are provided.

## Verify

```sh
./rootkitlings check L45
```

The verification opens a real TCP session and asks the shell for a runtime token, numeric user ID, and working directory.

It then unloads the module and confirms that the listener and helper terminate.

The listening socket remains visible in `/proc/net/tcp`, the helper appears as `/bin/sh`, and unencrypted session bytes are visible to packet capture.
