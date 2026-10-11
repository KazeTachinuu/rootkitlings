// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/inet.h>

#include "ipv4.h"

int rk_ipv4_parse(const char *text, __be32 *address)
{
	const char *end;
	__be32 parsed;

	if (!text || !address)
		return -EINVAL;
	if (!in4_pton(text, -1, (u8 *)&parsed, -1, &end) || *end || !parsed)
		return -EINVAL;
	*address = parsed;
	return 0;
}
