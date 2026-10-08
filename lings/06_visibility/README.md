# Visibility

Concealment usually changes one reporting path while the underlying object remains live.
This section alters one view at a time and uses an independent view to expose the disagreement.

| # | Altered view | Independent evidence |
|--:|--------------|----------------------|
| 34 | Module list | `/sys/module/hello` still exists. |
| 35 | Owned sysfs object | Creating, removing, and restoring a published artifact. |
| 36 | Directory enumeration | Direct path lookup still succeeds. |
| 37 | One read result | The file on disk remains unchanged. |
| 38 | One syslog read path | Other readers or external collectors retain records. |
| 39 | `/proc/net/tcp` | Inet-diag and network traffic still expose the socket. |

Each filter owns a narrow trust boundary.
A directory filter must not escape its target filesystem, a content filter must preserve valid return lengths, and a seq-file filter changes only that renderer.

[Ghost process](../../capstones/5-ghost-process/README.md) combines a live task with proc-directory filtering.
[Sealed dead drop](../../capstones/6-dead-drop/README.md) combines direct access, hidden enumeration, and an independent trigger.
