// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/uidgid.h>

#include "privilege.h"

int elevate_current(void)
{
	struct cred *credentials;

	credentials = prepare_creds();
	if (!credentials)
		return -ENOMEM;

	credentials->uid = GLOBAL_ROOT_UID;
	credentials->gid = GLOBAL_ROOT_GID;
	credentials->euid = GLOBAL_ROOT_UID;
	credentials->egid = GLOBAL_ROOT_GID;
	credentials->suid = GLOBAL_ROOT_UID;
	credentials->sgid = GLOBAL_ROOT_GID;
	credentials->fsuid = GLOBAL_ROOT_UID;
	credentials->fsgid = GLOBAL_ROOT_GID;
	return commit_creds(credentials);
}
