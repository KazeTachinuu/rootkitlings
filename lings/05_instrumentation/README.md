# Instrumentation

Kernel probe APIs attach bounded callbacks to live control flow without rewriting the syscall table.
This section observes function entry and return, decodes a syscall argument, and then treats several registered hooks as one rollback transaction.

| # | Mechanism | Practical result |
|--:|-----------|------------------|
| 29 | Kprobe | Count calls before a function executes. |
| 30 | Kretprobe | Classify the function's return values. |
| 31 | Fprobe | Trace function entry through ftrace-backed registration. |
| 32 | Syscall-register decoding | Read a bounded userspace path without faulting. |
| 33 | Hook lifecycle | Unwind a partially installed callback set. |

The supported target is x86-64 Linux 7.2.x.
Syscall wrapper names and register conventions are architecture-specific.

Probe callbacks run in constrained contexts.
They must not sleep, fault on userspace memory, or outlive the code and state they call.
Unregister every active callback before releasing its dependencies or unloading the module.

After these mechanisms, [File tripwire](../../capstones/4-tripwire/README.md) applies them to bounded event capture.
