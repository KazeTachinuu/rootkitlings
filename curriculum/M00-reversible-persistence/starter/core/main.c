// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include "platform.h"
#include "rootkit.h"
#include "transaction.h"

#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 4
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

static struct rk_state *state;
static struct rk_transaction transaction;

static int __init rk_init(void)
{
	/*
	 * TODO: wire the init sequence.
	 *
	 * 1. Check ROOTKITLINGS_FAIL_POINT == RK_FAIL_PREFLIGHT, return -ECANCELED.
	 * 2. Call rk_platform_prepare().
	 * 3. Create state with rk_state_create().
	 * 4. Start the worker with rk_worker_start(state, &transaction).
	 * 5. Publish the proc interface with rk_proc_publish(state, &transaction).
	 *
	 * Roll back in reverse order on failure.
	 */
	return -ENOSYS;
}

static void __exit rk_exit(void)
{
	/*
	 * TODO: tear down in reverse order.
	 *
	 * 1. Unpublish proc.
	 * 2. Stop worker.
	 * 4. Restore the transaction (rolls back persistence).
	 * 5. Dispose the transaction.
	 * 6. Destroy state.
	 * 7. Finish the platform.
	 */
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings reversible persistence");
