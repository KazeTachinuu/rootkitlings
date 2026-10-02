# Interfaces

Build a proper control surface for the module.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 09 | Error pointers | Distinguish `NULL`, `ERR_PTR`, valid. | Unchecked `ERR_PTR` dereference is a kernel crash. |
| 10 | Procfs write | Accept bounded userspace input. | Raw `copy_from_user` without length check overflows. |
| 11 | Lists | Intrusive `list_head` for ordered collections. | Concurrent list mutation without locking corrupts the list. |
| 12 | Char device | Register a `miscdevice` with open/release. | The device node is visible in `/dev` to any user. |
| 13 | Ioctl | Typed command interface through the device. | Ioctl numbers are guessable without authentication. |

Procfs is convenient but world-visible. A char device with ioctl gives a typed, permission-controlled
interface, which is what a rootkit control channel needs. But even ioctl has no authentication:
anyone with device access can send commands. That gap is closed in crypto (password/key derivation).
