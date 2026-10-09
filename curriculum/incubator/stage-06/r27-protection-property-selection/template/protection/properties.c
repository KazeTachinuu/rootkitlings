// SPDX-License-Identifier: GPL-2.0
#include "protection.h"

enum rk_xor_result
rk_xor_repeat(const unsigned char *input, size_t length,
	      const unsigned char *key, size_t key_length,
	      unsigned char *output, size_t capacity)
{
	return RK_XOR_INVALID_ARGUMENT;
}

enum rk_protection_plan
rk_choose_protection(unsigned int needs, unsigned int conditions)
{
	return RK_PROTECTION_UNAVAILABLE;
}
