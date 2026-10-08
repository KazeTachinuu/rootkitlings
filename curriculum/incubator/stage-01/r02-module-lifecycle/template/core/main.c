// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>

#include "rootkit.h"

static int __init rk_init(void)
{
	/* TODO: make this the module's successful initialization callback. */
	return 0;
}

static void __exit rk_exit(void)
{
	/* TODO: make this the module's exit callback. */
}

/* TODO: register both lifecycle callbacks and declare the required metadata. */
