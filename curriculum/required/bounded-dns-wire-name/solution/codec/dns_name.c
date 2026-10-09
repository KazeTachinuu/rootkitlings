// SPDX-License-Identifier: GPL-2.0
#include "dns_name.h"

enum rk_dns_name_result
rk_dns_name_encode(const struct rk_dns_label *labels, size_t label_count,
		   unsigned char *wire, size_t capacity, size_t *used)
{
	unsigned char candidate[RK_DNS_NAME_MAX];
	size_t index;
	size_t length = 0;

	if (!wire || !used || (label_count && !labels))
		return RK_DNS_NAME_INVALID_ARGUMENT;
	if (label_count > RK_DNS_LABELS_MAX)
		return RK_DNS_NAME_MALFORMED;

	for (index = 0; index < label_count; index++) {
		const struct rk_dns_label *label = &labels[index];

		if (!label->data)
			return RK_DNS_NAME_INVALID_ARGUMENT;
		if (!label->length || label->length > RK_DNS_LABEL_MAX)
			return RK_DNS_NAME_MALFORMED;
		if (label->length + 1 > RK_DNS_NAME_MAX - length - 1)
			return RK_DNS_NAME_MALFORMED;
		candidate[length++] = (unsigned char)label->length;
		memcpy(candidate + length, label->data, label->length);
		length += label->length;
	}
	candidate[length++] = 0;

	if (capacity < length)
		return RK_DNS_NAME_NO_SPACE;
	memcpy(wire, candidate, length);
	*used = length;
	return RK_DNS_NAME_OK;
}

enum rk_dns_name_result
rk_dns_name_parse(const unsigned char *wire, size_t available,
		  struct rk_dns_name_view *view)
{
	struct rk_dns_name_view parsed = { 0 };
	size_t offset = 0;

	if (!wire || !view)
		return RK_DNS_NAME_INVALID_ARGUMENT;

	for (;;) {
		unsigned char length;

		if (offset >= RK_DNS_NAME_MAX)
			return RK_DNS_NAME_MALFORMED;
		if (offset >= available)
			return RK_DNS_NAME_TRUNCATED;

		length = wire[offset];
		if (!length) {
			parsed.consumed = offset + 1;
			*view = parsed;
			return RK_DNS_NAME_OK;
		}
		if ((length & 0xc0) == 0xc0)
			return RK_DNS_NAME_UNSUPPORTED_COMPRESSION;
		if (length & 0xc0)
			return RK_DNS_NAME_MALFORMED;
		if ((size_t)length + 2 > RK_DNS_NAME_MAX - offset)
			return RK_DNS_NAME_MALFORMED;
		if ((size_t)length > available - offset - 1)
			return RK_DNS_NAME_TRUNCATED;
		if (parsed.label_count >= RK_DNS_LABELS_MAX)
			return RK_DNS_NAME_MALFORMED;

		parsed.data_offsets[parsed.label_count] = offset + 1;
		parsed.lengths[parsed.label_count] = length;
		parsed.label_count++;
		offset += (size_t)length + 1;
	}
}
