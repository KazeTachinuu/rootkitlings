# Minimal kernel VM

This directory contains shared course tooling.
The VM boots the host kernel in QEMU with a small BusyBox initramfs.
It has no disk and no network device.

Read [`boot`](boot) and [`init`](init) before using them.
The scripts print the important commands they run.

## Boot

```bash
./vm/boot
```

Inspect the guest:

```sh
uname -a
cat /proc/cmdline
cat /proc/1/status
mount
```

Exit the shell to power off the guest.

## Add a module

```bash
./vm/boot path/to/module.ko
```

The script copies the module to `/root` in the initramfs.
It does not load the module.
