# Procfs

Build one small text interface across the userspace/kernel boundary.

| # | Ling | One concept |
|--:|------|-------------|
| 09 | Proc entry | Create and remove one proc entry. |
| 10 | Read | Produce bounded text with `seq_file`. |
| 11 | Write | Validate userspace input, then commit it under a mutex. |

The sequence separates lifetime, output, and input so each callback has one job.

Continue with [Interfaces](../02_interfaces/README.md).
