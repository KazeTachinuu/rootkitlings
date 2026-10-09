// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/sched.h>

#include "rootkit.h"

int rk_credentials_replace_current(void)
{
	struct cred *replacement;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_CREDENTIAL_PREPARE)
		replacement = NULL;
	else
		/* Prepare one complete credential based on init_task. */
		replacement = NULL;
	if (!replacement)
		return -ENOMEM;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_CREDENTIAL_CANCEL) {
		abort_creds(replacement);
		return -ECANCELED;
	}

	/* Publish the replacement and return its result. */
	abort_creds(replacement);
	return -EOPNOTSUPP;
}
