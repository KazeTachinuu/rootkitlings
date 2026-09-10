#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>

#define BUF_SIZE 256

static char *buf;

static int __init memory_init(void)
{
	// TODO(rootkitlings): alloue BUF_SIZE octets avec kmalloc.
	//   buf = kmalloc(BUF_SIZE, GFP_KERNEL);
	//   si buf est NULL, affiche une erreur et retourne -ENOMEM.

	strscpy(buf, "rootkitlings/007: allocated OK", BUF_SIZE);
	printk(KERN_INFO "%s\n", buf);
	return 0;
}

static void __exit memory_exit(void)
{
	// TODO(rootkitlings): libere le buffer avec kfree.
	//   kfree(buf);

	printk(KERN_INFO "rootkitlings/007: freed\n");
}

module_init(memory_init);
module_exit(memory_exit);
MODULE_LICENSE("GPL");
