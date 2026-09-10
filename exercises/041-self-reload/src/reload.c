#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/umh.h>

static int __init reload_init(void)
{
	pr_info("rootkitlings/041: module loaded (self-reload active)\n");
	return 0;
}

static void __exit reload_exit(void)
{
	// TODO(rootkitlings): appelle call_usermodehelper pour executer /tmp/reload.sh
	//
	// Tu dois:
	// 1. Definir argv: {"/bin/bash", "/tmp/reload.sh", NULL}
	// 2. Definir envp: {"HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL}
	// 3. Appeler call_usermodehelper("/bin/bash", argv, envp, UMH_NO_WAIT)
	//
	// UMH_NO_WAIT car on est dans module_exit, on ne peut pas bloquer.
	// 6 lignes de code.

	pr_info("rootkitlings/041: module unloading, scheduling reload\n");
}

module_init(reload_init);
module_exit(reload_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rootkitlings 041: self-reloading module");
