# Interfaces

Build a proper control surface for the module.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 11 | Error pointers | Distinguish `NULL`, `ERR_PTR`, valid. | Unchecked `ERR_PTR` dereference is a kernel crash. |
| 12 | Procfs write | Accept bounded userspace input. | Raw `copy_from_user` without length check overflows. |
| 13 | Lists | Intrusive `list_head` for ordered collections. | Concurrent list mutation without locking corrupts the list. |
| 14 | Char device | Register a `miscdevice` with open/release. | The device node is visible in `/dev` to any user. |
| 15 | Ioctl | Typed command interface through the device. | Ioctl numbers are guessable without authentication. |
| 16 | Usermode exec | `call_usermodehelper` runs userspace programs from kernel. `filp_open`/`kernel_read` capture output. | Spawned process is visible in `ps`. Arguments visible in `/proc/pid/cmdline`. |

Procfs is convenient but world-visible. A char device with ioctl gives a typed, permission-controlled
interface, which is what a rootkit control channel needs. But even ioctl has no authentication:
anyone with device access can send commands. That gap is closed in crypto (password/key derivation).

Ling 16 teaches the kernel usermode-helper API for starting a userspace program:
Reptile launches its reverse shell, Pumakit runs operator commands, persistence modules write
boot scripts. The API is taught here because it is an interface (kernel calling out to userspace),
not a persistence mechanism.

After ling 16: build [Capstone 0: Config store](../../capstones/0-config-store/README.md).
