#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>
#include "ftrace_helper.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("Exercice 025: cleanup propre avec synchronize_rcu");

static asmlinkage long (*orig_openat2)(const struct pt_regs *);

static asmlinkage long hook_openat2(const struct pt_regs *regs)
{
    printk(KERN_INFO "rootkitlings/025: openat2 hooked (pid %d)\n",
           current->pid);
    return orig_openat2(regs);
}

static struct ftrace_hook hooks[] = {
    HOOK("do_sys_openat2", hook_openat2, &orig_openat2),
};

static int __init cleanup_init(void)
{
    int ret = fh_install_hooks(hooks, ARRAY_SIZE(hooks));

    if (ret)
        return ret;
    printk(KERN_INFO "rootkitlings/025: hook installed\n");
    return 0;
}

static void __exit cleanup_exit(void)
{
    fh_remove_hooks(hooks, ARRAY_SIZE(hooks));

    // TODO(rootkitlings): Ajoute synchronize_rcu() ici.
    //   Sans ca, un CPU pourrait encore etre dans hook_openat2
    //   quand le module est decharge. Sur une machine monocoeur
    //   c'est rare, mais sur SMP c'est un vrai risque de panic.
    //   Une seule ligne: synchronize_rcu();

    printk(KERN_INFO "rootkitlings/025: hook removed, cleanup done\n");
}

module_init(cleanup_init);
module_exit(cleanup_exit);
