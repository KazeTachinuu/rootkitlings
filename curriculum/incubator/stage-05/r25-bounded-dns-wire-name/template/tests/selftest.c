// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/string.h>

#include "dns_name.h"
#include "selftest.h"

static int rk_expect(bool condition, unsigned int *assertions)
{
	(*assertions)++;
	return condition ? 0 : -EINVAL;
}

int rk_dns_name_selftest(unsigned int *assertions)
{
	static const unsigned char first[] = "rootkitlings";
	static const unsigned char second[] = "local";
	static const unsigned char expected[] = {
		12, 'r', 'o', 'o', 't', 'k', 'i', 't', 'l', 'i', 'n', 'g', 's',
		5, 'l', 'o', 'c', 'a', 'l', 0,
	};
	const struct rk_dns_label labels[] = {
		{ first, sizeof(first) - 1 },
		{ second, sizeof(second) - 1 },
	};
	struct rk_dns_name_view view;
	unsigned char wire[sizeof(expected) + 1];
	size_t used = 0;

	*assertions = 0;
	memset(wire, 0xa5, sizeof(wire));
	if (rk_expect(rk_dns_name_encode(labels, ARRAY_SIZE(labels), wire,
					 sizeof(wire), &used) == RK_DNS_NAME_OK,
		      assertions) ||
	    rk_expect(used == sizeof(expected), assertions) ||
	    rk_expect(!memcmp(wire, expected, sizeof(expected)), assertions) ||
	    rk_expect(wire[sizeof(expected)] == 0xa5, assertions) ||
	    rk_expect(rk_dns_name_parse(wire, sizeof(wire), &view) ==
		      RK_DNS_NAME_OK, assertions) ||
	    rk_expect(view.label_count == 2 &&
		      view.data_offsets[0] == 1 && view.lengths[0] == 12 &&
		      view.data_offsets[1] == 14 && view.lengths[1] == 5 &&
		      view.consumed == sizeof(expected), assertions))
		return -EINVAL;

	return 0;
}
