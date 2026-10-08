# File I/O

Kernel file writes are durable side effects, so bounds, exact byte counts, ownership, and rollback matter more than convenience.

| # | Mechanism | Observable result |
|--:|-----------|-------------------|
| 48 | Reversible file transaction | A marker is prepended to a disposable file, then the original bytes are restored on unload. |

The module rejects an original that exceeds its saved buffer, treats a short write as an error, and never overwrites a file changed by another writer.

After ling 48, [Boot-time module request](../../capstones/8-boot-persistence/README.md) applies the same ownership rules to a durable `modules-load.d` request.
