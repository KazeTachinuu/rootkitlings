// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/uidgid.h>

#include "privilege.h"

int elevate_current(void)
{
	/* Prepare private credentials, set every identity, then commit them. */
	return -EOPNOTSUPP;
}
