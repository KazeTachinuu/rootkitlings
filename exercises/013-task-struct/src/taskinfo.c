#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("Exercice 013: lire struct task_struct via current");

static int __init taskinfo_init(void)
{
    printk(KERN_INFO "rootkitlings/013: reading current task_struct\n");

    // TODO(rootkitlings): Affiche le PID du processus courant.
    //   Utilise current->pid avec printk.
    //   Format: "rootkitlings/013: pid = %d\n"

    // TODO(rootkitlings): Affiche le nom du processus courant.
    //   Utilise current->comm avec printk.
    //   Format: "rootkitlings/013: comm = %s\n"

    // TODO(rootkitlings): Affiche le TGID (thread group ID).
    //   Utilise current->tgid avec printk.
    //   Format: "rootkitlings/013: tgid = %d\n"

    return 0;
}

static void __exit taskinfo_exit(void)
{
    printk(KERN_INFO "rootkitlings/013: unloaded\n");
}

module_init(taskinfo_init);
module_exit(taskinfo_exit);
