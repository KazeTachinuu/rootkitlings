#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include "ftrace_helper.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("Exercice 023: hooker kill, detecter signal 63");

#define MAGIC_SIGNAL 63

static asmlinkage long (*orig_kill)(const struct pt_regs *);

static asmlinkage long hook_kill(const struct pt_regs *regs)
{
    pid_t pid = regs->di;
    int sig = regs->si;

    // TODO(rootkitlings): Si sig == MAGIC_SIGNAL, affiche un message
    //   et retourne 0 (ne pas transmettre le signal).
    //   "rootkitlings/023: magic signal %d received for pid %d from %s\n"
    //   Sinon, appelle orig_kill(regs) normalement.

    return orig_kill(regs);
}

static struct ftrace_hook hooks[] = {
    HOOK("__x64_sys_kill", hook_kill, &orig_kill),
};

static int __init hookkill_init(void)
{
    int ret = fh_install_hooks(hooks, ARRAY_SIZE(hooks));

    if (ret)
        return ret;
    printk(KERN_INFO "rootkitlings/023: kill hook installed (magic signal = %d)\n",
           MAGIC_SIGNAL);
    return 0;
}

static void __exit hookkill_exit(void)
{
    fh_remove_hooks(hooks, ARRAY_SIZE(hooks));
    printk(KERN_INFO "rootkitlings/023: kill hook removed\n");
}

module_init(hookkill_init);
module_exit(hookkill_exit);
