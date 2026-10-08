# 48 - Apply and roll back a file edit

A reversible file transaction saves the exact original bytes, writes a known edited image, and restores the original only if the file still contains that image.

This module prepends `# inspection marker\n` to `/tmp/rk_victim.sh` and rolls the edit back on unload.

## Observe it

Create a disposable target and load the module:

```sh
printf '#!/bin/sh\necho original\n' > /tmp/rk_victim.sh
insmod hello.ko
cat /tmp/rk_victim.sh
cat /proc/rk_infect
```

```text
# inspection marker
#!/bin/sh
echo original

target=/tmp/rk_victim.sh
infected=yes
```

After `rmmod hello`, the target again contains exactly the original bytes.

## Transaction boundary

```text
read original -> build edited image -> exact write
                                          |
                                          v
unload -> compare current with edited image
              |                    |
            equal               changed
              |                    |
       restore original       preserve file
```

The initial read requests `ORIGINAL_MAX + 1` bytes.

Receiving more than `ORIGINAL_MAX` proves the target is too large and returns `-EFBIG` instead of silently truncating data needed for rollback.

`write_file()` accepts success only when `kernel_write()` returns the exact requested length.

The module keeps both the original and complete edited images for later comparison.

## Compare before rollback

`infected` records that this module successfully wrote its edited image.

The proc status independently reads the current file.

It reports `infected=yes` only when the current bytes still equal the saved edited image.

It reports `infected=changed` when another writer modifies or replaces the target after installation.

During unload, that same comparison prevents rollback from overwriting newer data that the module does not own.

## Implement it

The scaffold provides bounded reads, exact writes, proc reporting, comparison-before-rollback, and lifecycle cleanup.

In `infect()`, copy `PAYLOAD_LINE` into the start of `edited`.

Then copy all `orig_len` original bytes immediately after the payload.

Remove the marked safe failure so the supplied code calculates `edited_len`, writes the complete image, and sets `infected` only after that write succeeds.

## Verify

```sh
./rootkitlings check L48
```

The verification creates a runtime target, checks the exact edited bytes and `infected=yes`, unloads, and compares the restored file byte for byte.

It also changes the installed file, requires `infected=changed`, and proves unload preserves that newer content instead of forcing a rollback.

File-integrity monitoring and comparison with a trusted hash observe the edit independently of the module's proc status.

Next: [Capstone 8 - Boot-time module request](../../../capstones/8-boot-persistence/README.md).
