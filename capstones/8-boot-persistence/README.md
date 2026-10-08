# Boot-time module request

A loaded module disappears after reboot unless something requests it again.

On systems using `systemd-modules-load`, `/etc/modules-load.d/*.conf` files contain module names to request during boot.

This project installs and removes one such request without overwriting a file it does not own.

The request is not the module binary.

A real installation also needs `hello.ko` beneath `/lib/modules/<release>/` and an updated module dependency index.

The VM tests only the request-file transaction because it cannot honestly prove a later boot from the same temporary filesystem.

## Operate it

```sh
insmod hello.ko
cat /proc/rk_boot
```

```text
state=absent
path=/etc/modules-load.d/hello.conf
module=hello
```

Install the request:

```sh
echo install > /proc/rk_boot
cat /etc/modules-load.d/hello.conf
cat /proc/rk_boot
```

```text
hello

state=installed
path=/etc/modules-load.d/hello.conf
module=hello
```

Unload the module and inspect the file again:

```sh
rmmod hello
cat /etc/modules-load.d/hello.conf
```

The request remains because persistence that vanishes on unload is not persistence.

Reload the module and remove its request explicitly:

```sh
insmod hello.ko
echo remove > /proc/rk_boot
rmmod hello
```

## Ownership before convenience

```text
                           hello.conf
                               |
                   +-----------+-----------+
                   |                       |
                 absent                  exists
                   |                       |
                   v                       v
          create with O_EXCL       read exact content
                   |                       |
                   v                +------+------+
             write "hello\n"        |             |
                   |              "hello\n"      anything else
                   v                |             |
               installed         removable      foreign
                                                  |
                                             never modify
```

`O_CREAT | O_EXCL` makes installation fail if the path already exists.

It never truncates an administrator’s file.

Removal first reads the artifact and accepts only the exact bytes `hello\n`.

A missing, longer, shorter, or different file is not treated as ours.

## File transaction

Installation performs these steps in order:

```text
mkdir -p directory
        |
exclusive open
        |
exact kernel_write
        |
close
        |
short write? -- yes --> remove the partial file
        |
       no
        v
      success
```

`call_usermodehelper()` receives explicit argument vectors for `mkdir` and `rm`.

No shell interprets either command.

## Files

```text
main.c          proc commands and live state
persistence.c   inspect, install, roll back and remove the file
persistence.h   path, exact payload and state contract
```

## Your work

Complete `persistence_state()` after `kernel_read()`.

Return `PERSISTENCE_INSTALLED` only when the length equals `REQUEST_SIZE` and `memcmp()` confirms every byte of `MODULE_REQUEST`.

Return `PERSISTENCE_FOREIGN` for every other readable file.

The exclusive create, exact write check, partial-file rollback, and guarded removal are provided so the scaffold is safe to run before completion.

## Verify

```sh
./rootkitlings check C08
```

The check protects a pre-existing sentinel, verifies exact installation bytes, proves the request survives unload, reloads the module, removes the owned artifact, and confirms cleanup.

The configuration file is a durable and easily inspected defensive artifact.
