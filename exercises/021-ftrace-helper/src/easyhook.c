#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include "ftrace_helper.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("Exercice 021: hooking simplifie avec ftrace_helper.h");

/* Pointeur vers le kill() original */
static asmlinkage long (*orig_kill)(const struct pt_regs *);

// TODO(rootkitlings): Ecris la fonction hook_kill.
//   Signature: static asmlinkage long hook_kill(const struct pt_regs *regs)
//   Les arguments du syscall kill(pid, sig) sont dans:
//     pid_t pid = regs->di;    (premier argument)
//     int sig   = regs->si;    (deuxieme argument)
//   Affiche: "rootkitlings/021: kill(%d, %d) from %s\n", pid, sig, current->comm
//   Puis appelle et retourne orig_kill(regs).

static struct ftrace_hook hooks[] = {
    // TODO(rootkitlings): Declare le hook avec la macro HOOK.
    //   HOOK("__x64_sys_kill", hook_kill, &orig_kill),
};

static int __init easyhook_init(void)
{
    int ret;

    ret = fh_install_hooks(hooks, ARRAY_SIZE(hooks));
    if (ret)
        return ret;

    printk(KERN_INFO "rootkitlings/021: kill hook installed\n");
    return 0;
}

static void __exit easyhook_exit(void)
{
    fh_remove_hooks(hooks, ARRAY_SIZE(hooks));
    printk(KERN_INFO "rootkitlings/021: kill hook removed\n");
}

module_init(easyhook_init);
module_exit(easyhook_exit);
