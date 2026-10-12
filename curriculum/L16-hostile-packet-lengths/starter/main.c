// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#include "ipv4_parser.h"

static unsigned int rk_assertions;
static bool rk_parser_selftest;
module_param_named(parser_selftest, rk_parser_selftest, bool, 0444);
MODULE_PARM_DESC(parser_selftest,
		 "whether the bounded IPv4 parser self-test passed");

static struct rk_ipv4_view rk_sentinel(void)
{
	return (struct rk_ipv4_view) {
		.header_length = 101,
		.packet_length = 202,
		.payload_offset = 303,
		.payload_length = 404,
		.protocol = 0xa5,
	};
}

static bool rk_is_sentinel(const struct rk_ipv4_view *view)
{
	struct rk_ipv4_view expected = rk_sentinel();

	return !memcmp(view, &expected, sizeof(expected));
}

static int rk_expect(bool condition, const char *label)
{
	rk_assertions++;
	if (condition)
		return 0;
	pr_err("rklings: IPv4 parser self-test failed: %s\n", label);
	return -EINVAL;
}

static int rk_parser_self_test(void)
{
	unsigned char packet[20] = {
		0x45, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00,
		0x40, 0x11, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x01,
		0x7f, 0x00, 0x00, 0x01,
	};
	struct rk_ipv4_view view = rk_sentinel();
	enum rk_ipv4_parse_result result;
	int ret = 0;

	result = rk_parse_ipv4(packet, sizeof(packet), &view);
	ret |= rk_expect(result == RK_IPV4_PARSE_OK, "valid packet result");
	ret |= rk_expect(view.header_length == 20, "valid header length");
	ret |= rk_expect(view.packet_length == 20, "valid packet length");
	ret |= rk_expect(view.payload_offset == 20, "valid payload offset");
	ret |= rk_expect(view.payload_length == 0, "valid payload length");
	ret |= rk_expect(view.protocol == 17, "valid protocol");

	view = rk_sentinel();
	packet[3] = 21;
	result = rk_parse_ipv4(packet, sizeof(packet), &view);
	ret |= rk_expect(result == RK_IPV4_PARSE_TRUNCATED,
			 "declared packet truncation");
	ret |= rk_expect(rk_is_sentinel(&view), "truncation preserves output");

	view = rk_sentinel();
	packet[0] = 0x65;
	packet[3] = 20;
	result = rk_parse_ipv4(packet, sizeof(packet), &view);
	ret |= rk_expect(result == RK_IPV4_PARSE_MALFORMED,
			 "wrong IP version");
	ret |= rk_expect(rk_is_sentinel(&view), "malformed input preserves output");

	view = rk_sentinel();
	packet[0] = 0x45;
	packet[6] = 0x20;
	result = rk_parse_ipv4(packet, sizeof(packet), &view);
	ret |= rk_expect(result == RK_IPV4_PARSE_FRAGMENTED, "fragment policy");
	ret |= rk_expect(rk_is_sentinel(&view), "fragment preserves output");

	view = rk_sentinel();
	packet[6] = 0x80;
	result = rk_parse_ipv4(packet, sizeof(packet), &view);
	ret |= rk_expect(result == RK_IPV4_PARSE_OK,
			 "reserved flag follows Linux policy");
	ret |= rk_expect(view.protocol == 17, "reserved flag output");

	return ret;
}

static int __init rklings_init(void)
{
	int ret;

	ret = rk_parser_self_test();
	if (ret)
		return ret;
	rk_parser_selftest = true;
	pr_info("rklings: IPv4 parser self-tests passed: %u assertions\n",
		rk_assertions);
	return 0;
}

static void __exit rklings_exit(void)
{
}

module_init(rklings_init);
module_exit(rklings_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings bounded IPv4 parser");
