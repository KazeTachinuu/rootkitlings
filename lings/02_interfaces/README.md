# Interfaces

Build a proper control surface for the module.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 12 | Error pointers | Distinguish `NULL`, `ERR_PTR`, and valid pointers. | Dereferencing an unchecked `ERR_PTR` crashes the kernel. |
| 13 | Lists | Intrusive `list_head` for ordered collections. | Concurrent list mutation without locking corrupts the list. |
| 14 | Char device | Register a `miscdevice` with open/release. | The device node is visible in `/dev` to any user. |
| 15 | Ioctl | Typed command interface through the device. | Ioctl numbers are guessable without authentication. |
| 16 | Usermode helper | `call_usermodehelper` runs one explicit userspace program and waits for its result. | The process and its arguments are visible through ordinary process inspection. |

A character device with ioctl gives a typed, permission-controlled interface, but device access is not message authentication.

`call_usermodehelper()` crosses the boundary in the opposite direction by asking userspace to start a program.

Continue with [Concurrency](../03_concurrency/README.md).
Its synchronization rules are required by the first three capstones, including the concurrent configuration store.
