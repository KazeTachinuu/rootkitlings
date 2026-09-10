#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

static int __init persist_init(void)
{
	pr_info("rootkitlings/040: module loaded from initramfs\n");
	return 0;
}

static void __exit persist_exit(void)
{
	pr_info("rootkitlings/040: module unloaded\n");
}

module_init(persist_init);
module_exit(persist_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rootkitlings 040: initramfs persistence test module");
