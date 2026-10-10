// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/string.h>

#include "provision.h"

static int rk_hex_nibble(char character, u8 *nibble)
{
	if (character >= '0' && character <= '9')
		*nibble = character - '0';
	else if (character >= 'a' && character <= 'f')
		*nibble = character - 'a' + 10;
	else if (character >= 'A' && character <= 'F')
		*nibble = character - 'A' + 10;
	else
		return -EINVAL;
	return 0;
}

int rk_control_key_parse_and_clear(
	char *text, size_t capacity, u8 key[RK_AEAD_KEY_LENGTH])
{
	u8 candidate[RK_AEAD_KEY_LENGTH] = { };
	bool any_nonzero = false;
	size_t index;
	int error = -EINVAL;

	if (!text || !capacity || !key)
		return -EINVAL;
	if (strnlen(text, capacity) != RK_AEAD_KEY_LENGTH * 2)
		goto out;
	for (index = 0; index < RK_AEAD_KEY_LENGTH; index++) {
		u8 high;
		u8 low;

		if (rk_hex_nibble(text[index * 2], &high) ||
		    rk_hex_nibble(text[index * 2 + 1], &low))
			goto out;
		candidate[index] = (high << 4) | low;
		any_nonzero |= candidate[index] != 0;
	}
	if (!any_nonzero)
		goto out;
	memcpy(key, candidate, sizeof(candidate));
	error = 0;
out:
	memzero_explicit(candidate, sizeof(candidate));
	memzero_explicit(text, capacity);
	return error;
}
