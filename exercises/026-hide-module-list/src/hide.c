#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("026: hide module from lsmod");

static int __init hide_init(void)
{
    pr_info("rootkitlings/026: loading, about to hide from lsmod\n");

    // TODO(rootkitlings): retire THIS_MODULE de la liste des modules.
    //                     Indice: list_del prend un pointeur vers list_head.
    //                     1 ligne de code.

    pr_info("rootkitlings/026: hidden (check lsmod)\n");
    return 0;
}

static void __exit hide_exit(void)
{
    pr_info("rootkitlings/026: unloading\n");
}

module_init(hide_init);
module_exit(hide_exit);
