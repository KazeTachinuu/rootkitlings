#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ftrace.h>
#include <linux/kprobes.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("Exercice 020: hook ftrace complet avec redirection");

static unsigned long target_func_addr;

/* Pointeur vers la fonction originale */
static asmlinkage long (*orig_openat2)(const struct pt_regs *);

/* Lookup via kprobe (exo 012) */
static unsigned long lookup_name(const char *name)
{
    struct kprobe kp = { .symbol_name = name };
    unsigned long addr;
    int ret = register_kprobe(&kp);

    if (ret < 0)
        return 0;
    addr = (unsigned long)kp.addr;
    unregister_kprobe(&kp);
    return addr;
}

/* Ta fonction de remplacement */
static asmlinkage long hook_openat2(const struct pt_regs *regs)
{
    // TODO(rootkitlings): Affiche un log puis appelle l'originale.
    //   printk(KERN_INFO "rootkitlings/020: openat2 hooked\n");
    //   return orig_openat2(regs);

    return orig_openat2(regs); // remplace cette ligne par ton code
}

/* Callback ftrace */
static void notrace ftrace_callback(unsigned long ip,
                                    unsigned long parent_ip,
                                    struct ftrace_ops *ops,
                                    struct ftrace_regs *fregs)
{
    struct pt_regs *regs = ftrace_get_regs(fregs);

    if (!regs)
        return;

    // TODO(rootkitlings): Verifie que l'appel ne vient pas de TON module
    //   (anti-recursion), puis redirige vers hook_openat2.
    //   if (!within_module(parent_ip, THIS_MODULE))
    //       regs->ip = (unsigned long) hook_openat2;

}

static struct ftrace_ops ftrace_ops = {
    .func = ftrace_callback,
    .flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_IPMODIFY,
};

static int __init ftracehook_init(void)
{
    int ret;

    target_func_addr = lookup_name("do_sys_openat2");
    if (!target_func_addr) {
        printk(KERN_ERR "rootkitlings/020: do_sys_openat2 not found\n");
        return -ENOENT;
    }

    orig_openat2 = (void *)target_func_addr;

    ret = ftrace_set_filter_ip(&ftrace_ops, target_func_addr, 0, 0);
    if (ret < 0) {
        printk(KERN_ERR "rootkitlings/020: set_filter_ip failed: %d\n", ret);
        return ret;
    }

    ret = register_ftrace_function(&ftrace_ops);
    if (ret < 0) {
        printk(KERN_ERR "rootkitlings/020: register failed: %d\n", ret);
        return ret;
    }

    printk(KERN_INFO "rootkitlings/020: hook active on do_sys_openat2\n");
    return 0;
}

static void __exit ftracehook_exit(void)
{
    unregister_ftrace_function(&ftrace_ops);
    ftrace_set_filter_ip(&ftrace_ops, target_func_addr, 1, 0);
    printk(KERN_INFO "rootkitlings/020: hook removed\n");
}

module_init(ftracehook_init);
module_exit(ftracehook_exit);
