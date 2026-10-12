// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include "platform.h"
#include "rootkit.h"
#include "transaction.h"

#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 4
#error "unknown RKLINGS_FAIL_POINT"
#endif

static struct rk_state *state;
static struct rk_transaction transaction;

static int __init rk_init(void)
{
	int error;

	if (RKLINGS_FAIL_POINT == RK_FAIL_PREFLIGHT)
		return -ECANCELED;

	error = rk_platform_prepare();
	if (error)
		return error;

	state = rk_state_create();
	if (IS_ERR(state)) {
		error = PTR_ERR(state);
		state = NULL;
		goto out_platform;
	}

	error = rk_worker_start(state, &transaction);
	if (error)
		goto destroy_state;

	error = rk_proc_publish(state, &transaction);
	if (error)
		goto stop_worker;

	return 0;

stop_worker:
	rk_worker_stop();
destroy_state:
	rk_state_destroy(state);
	state = NULL;
out_platform:
	rk_platform_finish();
	return error;
}

static void __exit rk_exit(void)
{
	rk_proc_unpublish();
	rk_worker_stop();
	rk_transaction_restore(&transaction);
	rk_transaction_dispose(&transaction);
	rk_state_destroy(state);
	state = NULL;
	rk_platform_finish();
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings reversible persistence");
