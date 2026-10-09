// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/align.h>
#include <linux/dirent.h>
#include <linux/limits.h>
#include <linux/stddef.h>
#include <linux/string.h>

#include "dirent_filter.h"

#define RK_DIRENT_NAME_OFFSET offsetof(struct linux_dirent64, d_name)

static unsigned short rk_record_length(const unsigned char *record)
{
	unsigned short length;

	memcpy(&length,
	       record + offsetof(struct linux_dirent64, d_reclen),
	       sizeof(length));
	return length;
}

static bool rk_record_matches(const unsigned char *record, const char *target,
			      size_t target_length)
{
	const char *name = (const char *)record + RK_DIRENT_NAME_OFFSET;
	const size_t name_length = strlen(name);

	return name_length == target_length &&
	       memcmp(name, target, target_length) == 0;
}

enum rk_dirent_filter_result
rk_filter_dirents64(void *buffer, size_t length,
		    const char *target, size_t target_length,
		    size_t *output_length)
{
	unsigned char *bytes = buffer;
	size_t offset = 0;
	size_t write_offset = 0;
	size_t previous_retained = SIZE_MAX;
	size_t matches = 0;
	size_t records = 0;

	*output_length = length;
	while (offset < length) {
		const unsigned char *record = bytes + offset;
		const size_t remaining = length - offset;
		unsigned short record_length;
		const char *name;
		const char *terminator;
		size_t name_capacity;
		size_t name_length;
		size_t canonical_length;

		if (remaining < RK_DIRENT_NAME_OFFSET + 1)
			return RK_DIRENT_FILTER_INVALID;
		record_length = rk_record_length(record);
		if (record_length == 0 || record_length > remaining ||
		    record_length < RK_DIRENT_NAME_OFFSET + 1)
			return RK_DIRENT_FILTER_INVALID;
		name = (const char *)record + RK_DIRENT_NAME_OFFSET;
		name_capacity = record_length - RK_DIRENT_NAME_OFFSET;
		terminator = memchr(name, '\0', name_capacity);
		if (!terminator)
			return RK_DIRENT_FILTER_INVALID;
		name_length = (size_t)(terminator - name);
		if (name_length == 0 || name_length >= PATH_MAX ||
		    memchr(name, '/', name_length))
			return RK_DIRENT_FILTER_INVALID;
		canonical_length = ALIGN(RK_DIRENT_NAME_OFFSET + name_length + 1,
					 sizeof(u64));
		if (record_length != canonical_length)
			return RK_DIRENT_FILTER_INVALID;
		if (name_length == target_length &&
		    memcmp(name, target, target_length) == 0)
			matches++;
		records++;
		offset += record_length;
	}
	if (matches == 0 || matches == records)
		return RK_DIRENT_FILTER_UNCHANGED;

	offset = 0;
	while (offset < length) {
		unsigned char *record = bytes + offset;
		const unsigned short record_length = rk_record_length(record);

		if (rk_record_matches(record, target, target_length)) {
			if (previous_retained != SIZE_MAX)
				memcpy(bytes + previous_retained +
				       offsetof(struct linux_dirent64, d_off),
				       record + offsetof(struct linux_dirent64, d_off),
				       sizeof(((struct linux_dirent64 *)0)->d_off));
		} else {
			if (write_offset != offset)
				memmove(bytes + write_offset, record, record_length);
			previous_retained = write_offset;
			write_offset += record_length;
		}
		offset += record_length;
	}
	*output_length = write_offset;
	return RK_DIRENT_FILTER_CHANGED;
}
