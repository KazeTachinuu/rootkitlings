#include <linux/module.h>
#include <linux/init.h>

static int __init hello_init(void)
{
	pr_info("rootkitlings/001: hello from kernel space\n");
	pr_info("rootkitlings/001: PID du processus qui charge = %d (%s)\n",
		current->pid, current->comm);
	return 0;
}

static void __exit hello_exit(void)
{
	pr_info("rootkitlings/001: goodbye from kernel space\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("Premier module kernel, hello world");
