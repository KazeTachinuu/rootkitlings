# Hooking

Intercept kernel functions at runtime.
Hooking is the mechanism behind hiding (category 05): if your rootkit hides anything, you need this first.

| # | Ling | Teaches | Weakness exposed |
|--:|------|---------|-----------------|
| 29 | Tracefs | Read `/sys/kernel/tracing` to observe a function call path. | Tracefs reveals every hookable function. Defenders use it too. |
| 30 | Kprobe | `register_kprobe` on a function entry. | Kprobes are visible in `/sys/kernel/debug/kprobes/list`. |
| 31 | Kretprobe | Observe (and modify) a function's return value. | Return value hooks are powerful but stack-limited and visible. |
| 32 | Write protection | Bypass read-only page protection to patch kernel memory. | Direct CR0 manipulation may be blocked by hardware. Shadow mapping (`virt_to_phys` + `ioremap`) is portable. |
| 33 | Ftrace | `register_ftrace_function` with `SAVE_REGS`, `IPMODIFY`, `RECURSION`. | Ftrace hooks appear in `/sys/kernel/tracing/enabled_functions`. Volatility `linux_check_fops` detects hooked ops tables. |
| 34 | rmdir trigger | Hook `__x64_sys_rmdir`, parse path as command. Pumakit pattern. | Defenders rarely watch rmdir, but ftrace still shows the hook. |
| 35 | Hook lifecycle | Install multiple hooks, roll back on partial failure. | Forgetting one hook on unload leaves a dangling callback: instant crash. |

Ling 32 teaches the concept of write-protected kernel memory and two approaches to bypass it:
1. **CR0.WP clearing**: toggle the write-protect bit in the control register. Simple,
   but modern hardware features can trap this operation.
2. **Shadow mapping**: get the physical address with `virt_to_phys()`, create a second
   writable virtual mapping with `ioremap()`. This works regardless of hardware protections
   because it maps the same physical page at a new virtual address without the read-only flag.

The ling shows: try the direct approach, observe it may fail, use the portable alternative.
The concept is the same either way: kernel memory protection is a software/hardware policy,
not a physical property of the memory.

Ling 33 requires all three ftrace flags:
- `FTRACE_OPS_FL_SAVE_REGS`: save register state so the callback can read/modify arguments
- `FTRACE_OPS_FL_IPMODIFY`: allow the callback to redirect execution (change `regs->ip`)
- `FTRACE_OPS_FL_RECURSION`: mark the callback as recursion-aware

Without `IPMODIFY`, modifying `regs->ip` is undefined behavior. Real rootkits (PUMAKIT, KoviD,
Syslogk) all use this exact combination.

Modern rootkits prefer ftrace over syscall table patching. Ftrace is official kernel
infrastructure, works on hardened configs, and doesn't need write protection bypass.
But it's visible in tracefs. If that matters for your rootkit, module hiding (ling 36) covers it.

Note: eBPF (`fentry/fexit`, TC hooks, XDP) is the other major hooking mechanism,
used by BPFDoor and VoidLink. It needs no kernel module. This course focuses on
LKM techniques, which is what most real-world rootkits use.

Ling 34 teaches the Pumakit pattern: hook a benign syscall (`rmdir`) and use its
path argument as a command channel. `rmdir("/tmp/.rk-ping")` triggers the module,
but the directory is never removed. Defenders watch `kill` signals (Diamorphine
made that famous). Nobody watches `rmdir`.

After ling 35: build [Capstone 2: Tripwire](../../capstones/2-tripwire/README.md).
