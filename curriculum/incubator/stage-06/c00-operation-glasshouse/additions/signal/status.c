// SPDX-License-Identifier: GPL-2.0
#include "authenticated_dns.h"
#include "platform.h"
#include "status_signal.h"
#include "transport.h"

int rk_status_signal_send(const u8 *key, size_t key_length, u64 epoch,
			  u64 sequence, u8 status, u16 transaction_id)
{
	u8 packet[RK_DNS_SIGNAL_PACKET_LENGTH];
	size_t used = 0;
	int error;

	error = rk_dns_signal_build(key, key_length, epoch, sequence, status,
				    transaction_id, packet, sizeof(packet), &used);
	if (!error)
		error = rk_udp_send_loopback(packet, used);
	rk_platform_clear(packet, sizeof(packet));
	return error;
}
