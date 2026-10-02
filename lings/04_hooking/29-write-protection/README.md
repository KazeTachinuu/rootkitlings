# 29 - Write-protection bypass

## Why this step

The kernel marks certain memory pages as read-only (rodata, syscall
tables, function pointers). Rootkits bypass this to overwrite protected
data. Since kernel 5.3, CR0.WP is pinned and cannot be cleared. The
modern approach uses `set_memory_rw()` to manipulate page table entries.

## The concept

```
  Read-only page (after init):

  +------------------+
  | marker = 0xDEAD  |  <-- __ro_after_init
  +------------------+
         |
         v  direct write
       FAULT (page is read-only)

  Bypass via page table:

  1. Walk page table: CR3 -> PGD -> P4D -> PUD -> PMD -> PTE
  2. Flip PTE R/W bit: set_pte(ptep, pte_mkwrite(*ptep))
  3. Flush TLB: invlpg
  4. Write: WRITE_ONCE(marker, 0xCAFE)   <-- succeeds
  5. Restore PTE to read-only
```

Since kernel 5.3, CR0.WP is pinned and cannot be cleared. The modern
approach manipulates individual page table entries instead.

## What you already know

From ling 27, you used kprobes. This ling teaches you to bypass the
kernel's own memory protection on read-only pages.

## Build it

You need:

```c
#include <linux/pgtable.h>
```

Declare a variable in `__ro_after_init` aligned to a page boundary:

```c
static unsigned long marker __ro_after_init __aligned(PAGE_SIZE) = 0xDEADBEEF;
```

Walk the page table from CR3, find the PTE, flip its R/W bit:

```c
set_pte(ptep, pte_mkwrite_novma(*ptep));
asm volatile("invlpg (%0)" : : "r"(addr) : "memory");

WRITE_ONCE(*(unsigned long *)addr, 0xCAFEBABE);

set_pte(ptep, orig_pte);  // restore
```

Create `/proc/rk_wrprot` showing before/after values and whether the
modification succeeded.

## Output format

```
before=0xDEADBEEF
after=0xCAFEBABE
modified=yes
```

## Predict

- Why does the variable need page alignment?
- Why use `invlpg` after modifying the PTE?
- Why can't rootkits clear CR0.WP anymore (CR pinning)?


## Hints

<details>
<summary>Hint 1: walking the page table</summary>

To find the PTE for your marker's address, walk the four-level page
table from the kernel's PGD (init_mm.pgd):

```c
pgd -> p4d -> pud -> pmd -> pte
```

Each level has a lookup function (`pgd_offset`, `p4d_offset`,
`pud_offset`, `pmd_offset`, `pte_offset_kernel`). Check for "none" or
"bad" entries at each level.

</details>

<details>
<summary>Hint 2: flush the TLB after modifying the PTE</summary>

After changing the PTE with `set_pte`, the CPU may still cache the old
mapping. Use `invlpg` to flush that one TLB entry:

```c
asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
```

Without this, the write may still fault despite the PTE change.

</details>

<details>
<summary>Hint 3: restore the original PTE</summary>

Save the original PTE before modifying it. After your write, restore it
with `set_pte(ptep, orig_pte)` and flush the TLB again. Leaving pages
writable is a security hole and a detection signal.

</details>

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
dmesg | tail
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_wrprot` contains `before=0xDEADBEEF` and `after=0xCAFEBABE`.
- `modified=yes` confirms the write-protection bypass worked.

## Verify

```bash
./rootkitlings check 29
```

The check verifies the marker was 0xDEADBEEF before and 0xCAFEBABE
after, confirming the write-protection bypass worked.

> **What a defender sees**: `set_memory_rw` calls are logged by some
> integrity frameworks. W+X pages trigger alerts in lockdown mode.

See also: [30 - Ftrace](../30-ftrace/README.md).
