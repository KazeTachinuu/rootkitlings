# Operator

This is the canonical host-side operator used by P00.

It requests client certificates on the agent listener.
Certificates signed by the course CA enter the operator protocol; missing or
untrusted certificates receive the decoy website on the same TLS socket.
It also exposes the loopback browser dashboard and terminal console.
It never boots a VM or starts an agent.

The console keeps execution and terminal semantics distinct:

- `exec <command>` returns stdout, stderr, and exit code.
- `shell` opens the persistent remote PTY; `Ctrl+]` returns to the console.
- `download <path>` saves raw bytes below `.rklings/downloads/<target>/<path>`
  and prints the local path and size.

The browser keeps downloads in its explicit preview and save workflow.
The terminal never prints downloaded payload bytes.

Build it directly:

```sh
go build -trimpath -o operator ./cmd/server
```

The course CLI builds and configures it with:

```sh
./rklings serve P00
```

The M02 server remains inside M02 as the teaching implementation.
P00 depends on this product component instead of borrowing the milestone
solution.
