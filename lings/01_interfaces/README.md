# Interfaces

Build a proper control surface for the module.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 11 | Error pointers | Distinguish `NULL`, `ERR_PTR`, valid. | Unchecked `ERR_PTR` dereference is a kernel crash. |
| 12 | Procfs write | Accept bounded userspace input. | Raw `copy_from_user` without length check overflows. |
| 13 | Lists | Intrusive `list_head` for ordered collections. | Concurrent list mutation without locking corrupts the list. |
| 14 | Char device | Register a `miscdevice` with open/release. | The device node is visible in `/dev` to any user. |
| 15 | Ioctl | Typed command interface through the device. | Ioctl numbers are guessable without authentication. |

Procfs is convenient but world-visible. A char device with ioctl gives a typed, permission-controlled
interface, which is what a rootkit control channel needs. But even ioctl has no authentication:
anyone with device access can send commands. That gap is closed in crypto (password/key derivation).

After ling 15: build [Capstone 1: Command box](../../capstones/1-command-box/README.md).
