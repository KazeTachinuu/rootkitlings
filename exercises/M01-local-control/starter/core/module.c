// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>

// TODO: rk_module_hide (list_del THIS_MODULE)
void rk_module_hide(void)
{
}

// TODO: rk_module_unhide (list_add to saved prev)
void rk_module_unhide(void)
{
}

// TODO: rk_persist (filp_open + kernel_write PERSIST_PATH)

// TODO: rk_unpersist (truncate persistence file)

// TODO: command table and rk_module_cmds_init
int rk_module_cmds_init(void)
{
	return 0;
}
