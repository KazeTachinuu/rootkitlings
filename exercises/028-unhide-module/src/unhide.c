#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("028: hide module with clean rmmod restore");

static struct list_head *saved_prev;

static int __init unhide_init(void)
{
    pr_info("rootkitlings/028: loading\n");

    // TODO(rootkitlings): sauvegarde THIS_MODULE->list.prev dans saved_prev
    //                     AVANT de faire list_del.
    //                     1 ligne.

    // TODO(rootkitlings): retire le module de la liste (comme exercice 026).
    //                     1 ligne.

    pr_info("rootkitlings/028: hidden from lsmod (with restore ready)\n");
    return 0;
}

static void __exit unhide_exit(void)
{
    // TODO(rootkitlings): re-insere le module dans la liste pour que rmmod
    //                     puisse le trouver et faire le cleanup.
    //                     Utilise list_add avec saved_prev.
    //                     1 ligne.

    pr_info("rootkitlings/028: restored to module list, unloading cleanly\n");
}

module_init(unhide_init);
module_exit(unhide_exit);
