#include <linux/module.h>
#include <linux/init.h>

static int __init info_init(void)
{
	printk(KERN_INFO "rootkitlings/002: module loaded\n");
	return 0;
}

static void __exit info_exit(void)
{
	printk(KERN_INFO "rootkitlings/002: module unloaded\n");
}

module_init(info_init);
module_exit(info_exit);

// TODO(rootkitlings): ajoute les 4 macros de metadonnees:
//   MODULE_LICENSE("GPL")      - requis, sinon le kernel taint
//   MODULE_AUTHOR("ton nom")   - qui a ecrit ce module
//   MODULE_DESCRIPTION("...")  - description courte
//   MODULE_VERSION("0.1")      - version du module
