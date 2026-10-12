/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include "selftest.h"

#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 3
#error "unknown RKLINGS_FAIL_POINT"
#endif

static int __init rklings_init(void)
{
	int error = rk_aead_selftest();
	if (error) { pr_err("rklings: aead selftest failed: %d\n", error); return error; }
	pr_info("rklings: authenticated-encryption ready\n");
	return 0;
}
static void __exit rklings_exit(void)
{
	pr_info("rklings: authenticated-encryption stopped\n");
}
module_init(rklings_init);
module_exit(rklings_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings authenticated encryption lesson");
