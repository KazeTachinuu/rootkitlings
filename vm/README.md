# Minimal kernel VM

This directory contains shared course tooling.
The VM boots the pinned course kernel in QEMU with a small BusyBox initramfs.
`vm/kernel.toml` identifies the verified source archive, and `scripts/kernel ensure` builds the image and external-module tree locally.
The host kernel and host kernel headers are not used.
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

The default guest has only loopback.
For an experiment that must reach QEMU's user-network services, request the optional capability explicitly:

```bash
./rootkitlings run --cap network L19
```

The guest then uses `10.0.2.15`; the host-side QEMU alias is `10.0.2.2`.

## Add a module

```bash
./vm/boot path/to/module.ko
```

The shell opens in `/rootkitlings`, which contains only the module you built.
The script does not load it for you.

## Add guest tools

Request only the tools an exercise needs.
The option is repeatable and accepts either an executable filename or a repository-relative path under `vm/tools/`:

```bash
./vm/boot --tool netfilter_udp_test path/to/module.ko
```

Requested tools are built first and copied to `/bin` in the guest.
Unknown, non-executable, symlinked, or escaping paths are rejected; other host tools are not included.
