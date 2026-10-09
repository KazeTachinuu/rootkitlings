// SPDX-License-Identifier: GPL-2.0
#include "dns_name.h"

enum rk_dns_name_result
rk_dns_name_encode(const struct rk_dns_label *labels, size_t label_count,
		   unsigned char *wire, size_t capacity, size_t *used)
{
	/* Encode one complete, uncompressed DNS wire name. */
	(void)labels;
	(void)label_count;
	(void)wire;
	(void)capacity;
	(void)used;
	return RK_DNS_NAME_INVALID_ARGUMENT;
}

enum rk_dns_name_result
rk_dns_name_parse(const unsigned char *wire, size_t available,
		  struct rk_dns_name_view *view)
{
	/* Parse one complete, uncompressed DNS wire name. */
	(void)wire;
	(void)available;
	(void)view;
	return RK_DNS_NAME_INVALID_ARGUMENT;
}
