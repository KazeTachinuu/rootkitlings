// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>

#include "interface.h"
#include "platform.h"
#include "transaction.h"

static struct rk_transaction transaction;

static int __init rootkitlings_init(void)
{
	int error;

	error = rk_platform_prepare();
	if (error)
		return error;
	error = rk_transaction_apply(&transaction);
	if (error)
		goto out_finish;
	if (rk_platform_should_fail(RK_STEP_PUBLISH))
		error = -EIO;
	else
		error = rk_proc_init(&transaction);
	if (!error)
		return 0;
	rk_transaction_restore(&transaction);
out_finish:
	rk_transaction_dispose(&transaction);
	rk_platform_finish();
	return error;
}

static void __exit rootkitlings_exit(void)
{
	rk_proc_exit();
	rk_transaction_restore(&transaction);
	rk_transaction_dispose(&transaction);
	rk_platform_finish();
}

module_init(rootkitlings_init);
module_exit(rootkitlings_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings guarded file transaction");
