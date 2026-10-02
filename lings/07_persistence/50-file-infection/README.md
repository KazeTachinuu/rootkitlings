# 50 - File infection

## Why this step

Rootkits persist by injecting code into existing files. `filp_open` +
`kernel_write` can modify any file from kernel space. This is how a
rootkit injects a loader line into a shell script, patches a binary,
or adds an SSH key.

Reptile uses this to drop a loader script. This ling injects a payload
line into an existing script, preserving the original content.

## The concept

```
  Before infection:           After infection:

  /tmp/rk_victim.sh           /tmp/rk_victim.sh
  +-------------------+       +-------------------+
  | #!/bin/sh         |       | # rootkit was here |  <-- prepended
  | echo "original"   |       | #!/bin/sh         |
  +-------------------+       | echo "original"   |
                              +-------------------+

  On unload:
  +-------------------+
  | #!/bin/sh         |  <-- original restored
  | echo "original"   |
  +-------------------+
```

The module reads the file, saves the original, then writes the payload
line followed by the original content. On unload, it restores the saved
original.

## What you need

From ling 09, you know `proc_create_single` and `seq_file`.
This ling adds kernel-space file I/O for content injection.

```c
#include <linux/fs.h>      // filp_open, filp_close, kernel_write, kernel_read
```

## Build it

Create a module that:

1. On load, reads `/tmp/rk_victim.sh` and saves the original content.
2. Prepends a payload line (`# rootkit was here`) to the file.
3. Verifies the injection by reading the file back.
4. Exposes `/proc/rk_infect` with target path and infection status.
5. On unload, restores the original file content.

Reading and writing:

```c
struct file *f;
loff_t pos = 0;

f = filp_open(path, O_RDONLY, 0);
orig_len = kernel_read(f, original, sizeof(original) - 1, &pos);
filp_close(f, NULL);

f = filp_open(path, O_WRONLY | O_TRUNC, 0);
pos = 0;
kernel_write(f, PAYLOAD_LINE, PAYLOAD_LEN, &pos);
kernel_write(f, original, orig_len, &pos);
filp_close(f, NULL);
```

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
printf '#!/bin/sh\necho original\n' > /tmp/rk_victim.sh
chmod +x /tmp/rk_victim.sh
cat /tmp/rk_victim.sh           # shows original content
insmod hello.ko
cat /tmp/rk_victim.sh           # payload prepended to the file
cat /proc/rk_infect                 # infected=yes
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_infect` must output lines in this format:
  ```
  infected=yes
  target=/tmp/rk_victim.sh
  ```
- On load, the module prepends a payload line (`rootkit was here`) to `/tmp/rk_victim.sh`.
- The original content is preserved after the injected line.
- On unload, the module restores the original file content.

## Verify

```bash
./rootkitlings check 50
```

The check creates a victim script, loads the module, verifies the
payload was injected, and verifies the original content is restored
after unload.

> **What a defender sees**: file integrity monitoring (AIDE, OSSEC)
> detects modified files. Comparing file hashes against known-good
> baselines is the primary defense.

See also: [51 - Boot persistence](../51-boot-persistence/README.md).
