// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include "rootkit.h"

// TODO: Implement rk_init
// 1. Initialize all four target lists with rk_target_init.
// 2. Create /proc/rklings with rk_proc_init.
// 3. Register all six kretprobes with goto rollback:
//    rk_mkdir_probe -> rk_mkdirat_probe -> rk_getdents_probe
//    -> rk_read_probe -> rk_tcp4_probe -> rk_tcp6_probe
//    Each failure label unregisters the previous probe and falls through.
//    The last failure label calls rk_proc_exit.
static int __init rk_init(void)
{
	/* TODO: implement */
	return 0;
}

// TODO: Implement rk_exit
// 1. rk_uncloak() (in case module was cloaked).
// 2. Unregister all six probes in reverse order.
// 3. rk_proc_exit().
static void __exit rk_exit(void)
{
	/* TODO: implement */
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings local control milestone");
