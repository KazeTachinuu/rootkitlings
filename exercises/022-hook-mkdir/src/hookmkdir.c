#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include "ftrace_helper.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("Exercice 022: hooker mkdir pour logger les creations");

static asmlinkage long (*orig_mkdirat)(const struct pt_regs *);

static asmlinkage long hook_mkdirat(const struct pt_regs *regs)
{
    char dirname[256];
    long copied;

    // TODO(rootkitlings): Lis le nom du dossier depuis l'espace utilisateur.
    //   Le pathname est dans regs->si (deuxieme argument de mkdirat).
    //   Utilise strncpy_from_user(dirname, (const char __user *)regs->si, sizeof(dirname))
    //   Si copied > 0, affiche:
    //     "rootkitlings/022: mkdir %s by %s (pid %d)\n", dirname, current->comm, current->pid
    //   Puis appelle et retourne orig_mkdirat(regs).

    (void)dirname;
    (void)copied;
    return orig_mkdirat(regs);
}

static struct ftrace_hook hooks[] = {
    HOOK("__x64_sys_mkdirat", hook_mkdirat, &orig_mkdirat),
};

static int __init hookmkdir_init(void)
{
    int ret = fh_install_hooks(hooks, ARRAY_SIZE(hooks));

    if (ret)
        return ret;
    printk(KERN_INFO "rootkitlings/022: mkdir hook installed\n");
    return 0;
}

static void __exit hookmkdir_exit(void)
{
    fh_remove_hooks(hooks, ARRAY_SIZE(hooks));
    printk(KERN_INFO "rootkitlings/022: mkdir hook removed\n");
}

module_init(hookmkdir_init);
module_exit(hookmkdir_exit);
