// SPDX-License-Identifier: GPL-2.0
#include "dns_name.h"

enum rk_dns_name_result
rk_dns_name_encode(const struct rk_dns_label *labels, size_t label_count,
		   unsigned char *wire, size_t capacity, size_t *used)
{
	unsigned char encoded[RK_DNS_NAME_MAX];
	unsigned char *next = encoded;
	size_t remaining = sizeof(encoded);
	size_t index;

	if (!wire || !used || (label_count != 0 && !labels))
		return RK_DNS_NAME_INVALID_ARGUMENT;
	if (label_count > RK_DNS_LABELS_MAX)
		return RK_DNS_NAME_MALFORMED;

	for (index = 0; index != label_count; ++index) {
		size_t length = labels[index].length;

		if (!labels[index].data)
			return RK_DNS_NAME_INVALID_ARGUMENT;
		if (length == 0 || length > RK_DNS_LABEL_MAX)
			return RK_DNS_NAME_MALFORMED;
		if (remaining < length + 2)
			return RK_DNS_NAME_MALFORMED;
		*next++ = (unsigned char)length;
		memcpy(next, labels[index].data, length);
		next += length;
		remaining -= length + 1;
	}
	*next++ = 0;
	if (capacity < (size_t)(next - encoded))
		return RK_DNS_NAME_NO_SPACE;
	memcpy(wire, encoded, (size_t)(next - encoded));
	*used = (size_t)(next - encoded);
	return RK_DNS_NAME_OK;
}

enum rk_dns_name_result
rk_dns_name_parse(const unsigned char *wire, size_t available,
		  struct rk_dns_name_view *view)
{
	struct rk_dns_name_view result = { 0 };
	size_t remaining = available;
	size_t consumed = 0;

	if (!wire || !view)
		return RK_DNS_NAME_INVALID_ARGUMENT;

	for (;;) {
		unsigned char length;

		if (consumed == RK_DNS_NAME_MAX)
			return RK_DNS_NAME_MALFORMED;
		if (remaining == 0)
			return RK_DNS_NAME_TRUNCATED;
		length = wire[consumed];
		if (length == 0) {
			result.consumed = consumed + 1;
			*view = result;
			return RK_DNS_NAME_OK;
		}
		if ((length & 0xc0) == 0xc0)
			return RK_DNS_NAME_UNSUPPORTED_COMPRESSION;
		if ((length & 0xc0) != 0)
			return RK_DNS_NAME_MALFORMED;
		if ((size_t)length + 2 > RK_DNS_NAME_MAX - consumed)
			return RK_DNS_NAME_MALFORMED;
		if ((size_t)length + 1 > remaining)
			return RK_DNS_NAME_TRUNCATED;
		if (result.label_count == RK_DNS_LABELS_MAX)
			return RK_DNS_NAME_MALFORMED;

		result.data_offsets[result.label_count] = consumed + 1;
		result.lengths[result.label_count++] = length;
		consumed += (size_t)length + 1;
		remaining -= (size_t)length + 1;
	}
}
