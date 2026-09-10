/*
 * ftrace_helper.h
 *
 * Helper macros for ftrace-based function hooking in Linux kernel modules.
 * Based on the pattern by TheXcellerator (xcellerator.github.io).
 * Adapted for rootkitlings exercises.
 *
 * Usage:
 *   #include "ftrace_helper.h"
 *
 *   static asmlinkage long (*orig_func)(const struct pt_regs *);
 *   static asmlinkage long hook_func(const struct pt_regs *regs) {
 *       // your code
 *       return orig_func(regs);
 *   }
 *
 *   static struct ftrace_hook hooks[] = {
 *       HOOK("__x64_sys_funcname", hook_func, &orig_func),
 *   };
 *
 *   // In init: fh_install_hooks(hooks, ARRAY_SIZE(hooks))
 *   // In exit: fh_remove_hooks(hooks, ARRAY_SIZE(hooks))
 */

#ifndef FTRACE_HELPER_H
#define FTRACE_HELPER_H

#include <linux/ftrace.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/version.h>

struct ftrace_hook {
    const char *name;
    void *function;
    void *original;
    unsigned long address;
    struct ftrace_ops ops;
};

#define HOOK(_name, _hook, _orig) \
{                                 \
    .name = (_name),              \
    .function = (_hook),          \
    .original = (_orig),          \
}

/* Resolve symbol address via kprobe trick (works on kernels >= 5.7) */
static unsigned long fh_lookup_name(const char *name)
{
    struct kprobe kp = { .symbol_name = name };
    unsigned long addr;
    int ret;

    ret = register_kprobe(&kp);
    if (ret < 0)
        return 0;
    addr = (unsigned long)kp.addr;
    unregister_kprobe(&kp);
    return addr;
}

/* Ftrace callback: redirects execution to the hook function */
static void notrace fh_ftrace_thunk(unsigned long ip,
                                    unsigned long parent_ip,
                                    struct ftrace_ops *ops,
                                    struct ftrace_regs *fregs)
{
    struct pt_regs *regs = ftrace_get_regs(fregs);
    struct ftrace_hook *hook = container_of(ops, struct ftrace_hook, ops);

    if (!regs)
        return;

    /* Avoid recursion: don't hook calls from within this module */
    if (!within_module(parent_ip, THIS_MODULE))
        regs->ip = (unsigned long)hook->function;
}

/* Resolve and install a single hook */
static int fh_install_hook(struct ftrace_hook *hook)
{
    int ret;

    hook->address = fh_lookup_name(hook->name);
    if (!hook->address) {
        pr_err("ftrace_helper: symbol '%s' not found\n", hook->name);
        return -ENOENT;
    }

    /* Store the original function pointer */
    *((unsigned long *)hook->original) = hook->address;

    hook->ops.func = fh_ftrace_thunk;
    hook->ops.flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_IPMODIFY;

    ret = ftrace_set_filter_ip(&hook->ops, hook->address, 0, 0);
    if (ret) {
        pr_err("ftrace_helper: ftrace_set_filter_ip failed for %s: %d\n",
               hook->name, ret);
        return ret;
    }

    ret = register_ftrace_function(&hook->ops);
    if (ret) {
        pr_err("ftrace_helper: register_ftrace_function failed for %s: %d\n",
               hook->name, ret);
        ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
        return ret;
    }

    return 0;
}

/* Remove a single hook */
static void fh_remove_hook(struct ftrace_hook *hook)
{
    unregister_ftrace_function(&hook->ops);
    ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
}

/* Install an array of hooks */
static int fh_install_hooks(struct ftrace_hook *hooks, size_t count)
{
    size_t i;
    int ret;

    for (i = 0; i < count; i++) {
        ret = fh_install_hook(&hooks[i]);
        if (ret)
            goto err;
    }
    return 0;

err:
    while (i > 0)
        fh_remove_hook(&hooks[--i]);
    return ret;
}

/* Remove an array of hooks */
static void fh_remove_hooks(struct ftrace_hook *hooks, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
        fh_remove_hook(&hooks[i]);
}

#endif /* FTRACE_HELPER_H */
