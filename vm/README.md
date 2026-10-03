# Minimal kernel VM

This directory contains shared course tooling.
The VM boots the host kernel in QEMU with a small BusyBox initramfs.
It has no disk or network device unless networking is requested explicitly.

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

Add a QEMU user-network device when a networking lesson needs one:

```bash
./rootkitlings run --network
```

The guest uses `10.0.2.15`; the host is reachable at `10.0.2.2`.

## Add a module

```bash
./vm/boot path/to/module.ko
```

The shell opens in `/rootkitlings`, which contains only the module you built.
The script does not load it for you.
