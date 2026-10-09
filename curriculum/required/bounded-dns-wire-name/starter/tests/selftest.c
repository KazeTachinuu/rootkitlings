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

#define RK_EXPECT(condition) do { \
	if (rk_expect((condition), assertions)) \
		return -EINVAL; \
} while (0)

static size_t rk_make_prefix(unsigned char *wire, size_t target)
{
	size_t offset = 0;

	while (offset < target) {
		size_t remaining = target - offset;
		size_t span = remaining <= 64 ? remaining : 64;

		if (remaining > 64 && remaining - 64 == 1)
			span = 63;
		wire[offset++] = (unsigned char)(span - 1);
		memset(wire + offset, 0x6b, span - 1);
		offset += span - 1;
	}
	return offset;
}

int rk_dns_name_selftest(unsigned int *assertions)
{
	static struct rk_dns_label labels[RK_DNS_LABELS_MAX + 1];
	static struct rk_dns_name_view view, saved_view;
	static unsigned char wire[320], saved_wire[320];
	static unsigned char data[RK_DNS_LABEL_MAX];
	static const unsigned char expected[] = {
		12, 'r', 'o', 'o', 't', 'k', 'i', 't', 'l', 'i', 'n', 'g', 's',
		5, 'l', 'o', 'c', 'a', 'l', 0,
	};
	static const unsigned char first[] = "rootkitlings";
	static const unsigned char second[] = "local";
	enum rk_dns_name_result result, wanted;
	size_t available, index, used;
	unsigned int octet;

	*assertions = 0;
	memset(data, 0x5a, sizeof(data));
	labels[0] = (struct rk_dns_label){ first, sizeof(first) - 1 };
	labels[1] = (struct rk_dns_label){ second, sizeof(second) - 1 };
	memset(wire, 0xa5, sizeof(wire));
	used = 0;
	RK_EXPECT(rk_dns_name_encode(labels, 2, wire, sizeof(wire), &used) ==
		  RK_DNS_NAME_OK);
	RK_EXPECT(used == sizeof(expected));
	RK_EXPECT(!memcmp(wire, expected, sizeof(expected)));
	RK_EXPECT(wire[sizeof(expected)] == 0xa5);
	RK_EXPECT(rk_dns_name_parse(wire, sizeof(wire), &view) ==
		  RK_DNS_NAME_OK);
	RK_EXPECT(view.label_count == 2 && view.data_offsets[0] == 1 &&
		  view.lengths[0] == 12 && view.data_offsets[1] == 14 &&
		  view.lengths[1] == 5 && view.consumed == sizeof(expected));

	memset(wire, 0x5a, sizeof(wire));
	used = 77;
	RK_EXPECT(rk_dns_name_encode(NULL, 0, wire, sizeof(wire), &used) ==
		  RK_DNS_NAME_OK);
	RK_EXPECT(used == 1 && wire[0] == 0 && wire[1] == 0x5a);
	wire[1] = 0xc0;
	RK_EXPECT(rk_dns_name_parse(wire, sizeof(wire), &view) ==
		  RK_DNS_NAME_OK);
	RK_EXPECT(view.label_count == 0 && view.consumed == 1);

	memset(&view, 0xa5, sizeof(view));
	saved_view = view;
	RK_EXPECT(rk_dns_name_parse(NULL, 0, &view) ==
		  RK_DNS_NAME_INVALID_ARGUMENT);
	RK_EXPECT(!memcmp(&view, &saved_view, sizeof(view)));
	RK_EXPECT(rk_dns_name_parse(wire, sizeof(wire), NULL) ==
		  RK_DNS_NAME_INVALID_ARGUMENT);
	used = 77;
	RK_EXPECT(rk_dns_name_encode(NULL, 0, NULL, 0, &used) ==
		  RK_DNS_NAME_INVALID_ARGUMENT);
	RK_EXPECT(used == 77);

	for (octet = 0; octet <= 255; octet++) {
		wire[0] = (unsigned char)octet;
		memset(&view, 0xa5, sizeof(view));
		saved_view = view;
		result = rk_dns_name_parse(wire, 1, &view);
		if (octet == 0) {
			RK_EXPECT(result == RK_DNS_NAME_OK);
			RK_EXPECT(view.label_count == 0 && view.consumed == 1);
		} else {
			wanted = octet <= RK_DNS_LABEL_MAX ? RK_DNS_NAME_TRUNCATED :
				 octet < 0xc0 ? RK_DNS_NAME_MALFORMED :
				 RK_DNS_NAME_UNSUPPORTED_COMPRESSION;
			RK_EXPECT(result == wanted);
			RK_EXPECT(!memcmp(&view, &saved_view, sizeof(view)));
		}
	}

	for (index = 0; index < RK_DNS_LABELS_MAX; index++)
		labels[index] = (struct rk_dns_label){ data, 1 };
	used = 0;
	RK_EXPECT(rk_dns_name_encode(labels, RK_DNS_LABELS_MAX, wire,
				     RK_DNS_NAME_MAX, &used) == RK_DNS_NAME_OK);
	RK_EXPECT(used == RK_DNS_NAME_MAX);
	RK_EXPECT(rk_dns_name_parse(wire, used, &view) == RK_DNS_NAME_OK);
	RK_EXPECT(view.label_count == RK_DNS_LABELS_MAX &&
		  view.consumed == RK_DNS_NAME_MAX);
	for (index = 0; index < RK_DNS_LABELS_MAX; index++)
		RK_EXPECT(view.data_offsets[index] == index * 2 + 1 &&
			  view.lengths[index] == 1);
	for (available = 0; available < RK_DNS_NAME_MAX; available++) {
		memset(&view, 0xa5, sizeof(view));
		saved_view = view;
		RK_EXPECT(rk_dns_name_parse(wire, available, &view) ==
			  RK_DNS_NAME_TRUNCATED);
		RK_EXPECT(!memcmp(&view, &saved_view, sizeof(view)));
	}

	memcpy(saved_wire, wire, sizeof(wire));
	used = 0xa5a5;
	RK_EXPECT(rk_dns_name_encode(labels, RK_DNS_LABELS_MAX, wire,
				     RK_DNS_NAME_MAX - 1, &used) ==
		  RK_DNS_NAME_NO_SPACE);
	RK_EXPECT(used == 0xa5a5);
	RK_EXPECT(!memcmp(wire, saved_wire, sizeof(wire)));
	labels[RK_DNS_LABELS_MAX] = (struct rk_dns_label){ data, 1 };
	RK_EXPECT(rk_dns_name_encode(labels, RK_DNS_LABELS_MAX + 1, wire,
				     sizeof(wire), &used) ==
		  RK_DNS_NAME_MALFORMED);
	labels[0] = (struct rk_dns_label){ NULL, 0 };
	RK_EXPECT(rk_dns_name_encode(labels, 1, wire, sizeof(wire), &used) ==
		  RK_DNS_NAME_INVALID_ARGUMENT);
	labels[0] = (struct rk_dns_label){ data, 0 };
	RK_EXPECT(rk_dns_name_encode(labels, 1, wire, sizeof(wire), &used) ==
		  RK_DNS_NAME_MALFORMED);
	labels[0].length = RK_DNS_LABEL_MAX + 1;
	RK_EXPECT(rk_dns_name_encode(labels, 1, wire, sizeof(wire), &used) ==
		  RK_DNS_NAME_MALFORMED);

	memset(wire, 0xcc, sizeof(wire));
	memcpy(wire + 100, "first", 5);
	memcpy(wire + 2, "second", 6);
	labels[0] = (struct rk_dns_label){ wire + 100, 5 };
	labels[1] = (struct rk_dns_label){ wire + 2, 6 };
	RK_EXPECT(rk_dns_name_encode(labels, 2, wire, sizeof(wire), &used) ==
		  RK_DNS_NAME_OK);
	RK_EXPECT(used == 14 && wire[0] == 5 &&
		  !memcmp(wire + 1, "first", 5) && wire[6] == 6 &&
		  !memcmp(wire + 7, "second", 6) && wire[13] == 0);

	RK_EXPECT(rk_make_prefix(wire, RK_DNS_NAME_MAX) == RK_DNS_NAME_MAX);
	memset(&view, 0xa5, sizeof(view));
	saved_view = view;
	RK_EXPECT(rk_dns_name_parse(wire, RK_DNS_NAME_MAX, &view) ==
		  RK_DNS_NAME_MALFORMED);
	RK_EXPECT(!memcmp(&view, &saved_view, sizeof(view)));
	RK_EXPECT(rk_make_prefix(wire, 254) == 254);
	RK_EXPECT(rk_dns_name_parse(wire, 254, &view) ==
		  RK_DNS_NAME_TRUNCATED);
	wire[254] = 0xc0;
	RK_EXPECT(rk_dns_name_parse(wire, 255, &view) ==
		  RK_DNS_NAME_UNSUPPORTED_COMPRESSION);
	return 0;
}
