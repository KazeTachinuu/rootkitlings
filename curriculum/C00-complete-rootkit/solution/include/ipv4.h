/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_IPV4_H
#define ROOTKITLINGS_IPV4_H

#include <linux/types.h>

int rk_ipv4_parse(const char *text, __be32 *address);

#endif
