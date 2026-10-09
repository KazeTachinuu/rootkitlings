/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_DNS_NAME_H
#define ROOTKITLINGS_DNS_NAME_H

#ifdef __KERNEL__
#include <linux/stddef.h>
#include <linux/types.h>
#else
#include <stddef.h>
#endif

enum {
	RK_DNS_LABEL_MAX = 63,
	RK_DNS_NAME_MAX = 255,
	RK_DNS_LABELS_MAX = 127,
};

enum rk_dns_name_result {
	RK_DNS_NAME_OK,
	RK_DNS_NAME_INVALID_ARGUMENT,
	RK_DNS_NAME_TRUNCATED,
	RK_DNS_NAME_MALFORMED,
	RK_DNS_NAME_UNSUPPORTED_COMPRESSION,
	RK_DNS_NAME_NO_SPACE,
};

struct rk_dns_label {
	const unsigned char *data;
	size_t length;
};

struct rk_dns_name_view {
	size_t label_count;
	size_t consumed;
	size_t data_offsets[RK_DNS_LABELS_MAX];
	unsigned char lengths[RK_DNS_LABELS_MAX];
};

enum rk_dns_name_result
rk_dns_name_encode(const struct rk_dns_label *labels, size_t label_count,
		   unsigned char *wire, size_t capacity, size_t *used);

enum rk_dns_name_result
rk_dns_name_parse(const unsigned char *wire, size_t available,
		  struct rk_dns_name_view *view);

#endif
