// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/random.h>

#include "authenticated_dns.h"
#include "selftest.h"
#include "transport.h"

#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 3
#error "ROOTKITLINGS_FAIL_POINT must be between 0 and 3"
#endif

static unsigned long epoch = 0x0102030405060708UL;
static unsigned long sequence = 1;
static unsigned int status = 2;
static unsigned short transaction_id;

module_param(epoch, ulong, 0400);
module_param(sequence, ulong, 0400);
module_param(status, uint, 0400);
module_param(transaction_id, ushort, 0400);

static const u8 lab_key[RK_AUTH_KEY_LENGTH] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

static int __init rootkitlings_init(void)
{
	u8 packet[RK_DNS_SIGNAL_PACKET_LENGTH];
	unsigned int assertions;
	size_t used = 0;
	u16 id = transaction_id ? transaction_id : get_random_u16();
	int error;

	if (status > RK_AUTH_LEVEL_MAX)
		return -EINVAL;
	error = rk_dns_signal_selftest(&assertions);
	if (error)
		return error;
	error = rk_dns_signal_build(lab_key, sizeof(lab_key), epoch, sequence,
				    status, id, packet, sizeof(packet), &used);
	if (error)
		return error;
	error = rk_udp_send_loopback(packet, used);
	if (error)
		return error;
	pr_info("rootkitlings: authenticated DNS signal passed: %u assertions; sent=%zu id=%u\n",
		assertions, used, id);
	return 0;
}

static void __exit rootkitlings_exit(void)
{
}

module_init(rootkitlings_init);
module_exit(rootkitlings_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings authenticated DNS-shaped signal");
