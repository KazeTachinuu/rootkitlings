#include <linux/module.h>

MODULE_LICENSE("GPL");

static int __init capstone_init(void)
{
	return 0;
}

static void __exit capstone_exit(void)
{
}

module_init(capstone_init);
module_exit(capstone_exit);
