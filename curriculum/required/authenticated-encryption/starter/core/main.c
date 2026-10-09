/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include "selftest.h"

static int __init rootkitlings_init(void)
{
	int error = rk_aead_selftest();
	if (error) { pr_err("rootkitlings: aead selftest failed: %d\n", error); return error; }
	pr_info("rootkitlings: authenticated-encryption ready\n");
	return 0;
}
static void __exit rootkitlings_exit(void)
{
	pr_info("rootkitlings: authenticated-encryption stopped\n");
}
module_init(rootkitlings_init);
module_exit(rootkitlings_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings authenticated encryption lesson");
