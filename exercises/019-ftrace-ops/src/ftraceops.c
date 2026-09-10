#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ftrace.h>
#include <linux/kprobes.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("Exercice 019: enregistrer une callback ftrace");

static unsigned long target_func_addr;

/* Obtenir l'adresse d'une fonction via le trick kprobe (exo 012) */
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

// TODO(rootkitlings): Ecris la callback ftrace.
//   Signature:
//   static void notrace ftrace_callback(unsigned long ip,
//                                       unsigned long parent_ip,
//                                       struct ftrace_ops *ops,
//                                       struct ftrace_regs *fregs)
//   Dedans, fais un printk: "rootkitlings/019: hooked function called\n"
//   Le mot-cle 'notrace' empeche ftrace de tracer cette callback elle-meme.

static struct ftrace_ops ftrace_ops = {
    // TODO(rootkitlings): Assigne .func a ta callback
    .flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_IPMODIFY,
};

static int __init ftraceops_init(void)
{
    int ret;

    target_func_addr = lookup_name("do_sys_openat2");
    if (!target_func_addr) {
        printk(KERN_ERR "rootkitlings/019: do_sys_openat2 not found\n");
        return -ENOENT;
    }
    printk(KERN_INFO "rootkitlings/019: do_sys_openat2 at 0x%lx\n",
           target_func_addr);

    // TODO(rootkitlings): Configure le filtre ftrace pour ne hooker que
    //   do_sys_openat2, puis enregistre la callback.
    //   1. ret = ftrace_set_filter_ip(&ftrace_ops, target_func_addr, 0, 0);
    //      Si ret < 0, return ret.
    //   2. ret = register_ftrace_function(&ftrace_ops);
    //      Si ret < 0, return ret.

    printk(KERN_INFO "rootkitlings/019: ftrace hook registered\n");
    return 0;
}

static void __exit ftraceops_exit(void)
{
    unregister_ftrace_function(&ftrace_ops);
    ftrace_set_filter_ip(&ftrace_ops, target_func_addr, 1, 0);
    printk(KERN_INFO "rootkitlings/019: ftrace hook removed\n");
}

module_init(ftraceops_init);
module_exit(ftraceops_exit);
