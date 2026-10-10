// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>

#include "secure_control.h"

void rk_secure_clear(void *buffer, size_t length)
{
	memzero_explicit(buffer, length);
}
