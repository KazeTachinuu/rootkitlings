#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/align.h>
#include <linux/dirent.h>
#include "dirent_filter.h"

#define TARGET "rootkitlings_hidden"
#define NAME_OFFSET offsetof(struct linux_dirent64, d_name)

struct arena {
	unsigned char bytes[65536];
	size_t length;
};

static size_t add_record(struct arena *arena, const char *name, int64_t cookie,
			 unsigned char padding)
{
	const size_t name_length = strlen(name);
	const size_t length = ALIGN(NAME_OFFSET + name_length + 1, sizeof(uint64_t));
	unsigned char *record = arena->bytes + arena->length;
	unsigned short reclen = (unsigned short)length;
	uint64_t inode = arena->length + 11;

	memset(record, padding, length);
	memcpy(record + offsetof(struct linux_dirent64, d_ino), &inode, sizeof(inode));
	memcpy(record + offsetof(struct linux_dirent64, d_off), &cookie, sizeof(cookie));
	memcpy(record + offsetof(struct linux_dirent64, d_reclen), &reclen, sizeof(reclen));
	record[offsetof(struct linux_dirent64, d_type)] = 8;
	memcpy(record + NAME_OFFSET, name, name_length + 1);
	arena->length += length;
	return length;
}

static int64_t cookie_at(const unsigned char *bytes, size_t offset)
{
	int64_t value;
	memcpy(&value, bytes + offset + offsetof(struct linux_dirent64, d_off),
	       sizeof(value));
	return value;
}

static unsigned short reclen_at(const unsigned char *bytes, size_t offset)
{
	unsigned short value;
	memcpy(&value, bytes + offset + offsetof(struct linux_dirent64, d_reclen),
	       sizeof(value));
	return value;
}

static void expect_unchanged(struct arena *arena,
			     enum rk_dirent_filter_result expected)
{
	unsigned char before[sizeof(arena->bytes)];
	size_t output = SIZE_MAX;

	memcpy(before, arena->bytes, arena->length);
	assert(rk_filter_dirents64(arena->bytes, arena->length, TARGET,
				   sizeof(TARGET) - 1, &output) == expected);
	assert(output == arena->length);
	assert(memcmp(before, arena->bytes, arena->length) == 0);
}

static void basic_cases(void)
{
	struct arena arena = {0};
	size_t output;
	size_t first;

	expect_unchanged(&arena, RK_DIRENT_FILTER_UNCHANGED);
	add_record(&arena, "kept", 10, 0xa5);
	expect_unchanged(&arena, RK_DIRENT_FILTER_UNCHANGED);

	memset(&arena, 0, sizeof(arena));
	add_record(&arena, TARGET, 10, 0xa1);
	expect_unchanged(&arena, RK_DIRENT_FILTER_UNCHANGED);
	add_record(&arena, TARGET, 20, 0xb2);
	expect_unchanged(&arena, RK_DIRENT_FILTER_UNCHANGED);

	memset(&arena, 0, sizeof(arena));
	first = add_record(&arena, TARGET, 10, 0x11);
	add_record(&arena, "a-very-much-longer-retained-name", 20, 0x22);
	assert(rk_filter_dirents64(arena.bytes, arena.length, TARGET,
				   sizeof(TARGET) - 1, &output) ==
	       RK_DIRENT_FILTER_CHANGED);
	assert(output == arena.length - first);
	assert(strcmp((char *)arena.bytes + NAME_OFFSET,
		      "a-very-much-longer-retained-name") == 0);

	memset(&arena, 0, sizeof(arena));
	first = add_record(&arena, "first", 11, 0xcc);
	add_record(&arena, TARGET, 22, 0xdd);
	add_record(&arena, TARGET, 33, 0xee);
	add_record(&arena, "last", 44, 0xff);
	assert(rk_filter_dirents64(arena.bytes, arena.length, TARGET,
				   sizeof(TARGET) - 1, &output) ==
	       RK_DIRENT_FILTER_CHANGED);
	assert(cookie_at(arena.bytes, 0) == 33);
	assert(strcmp((char *)arena.bytes + first + NAME_OFFSET, "last") == 0);
	assert(cookie_at(arena.bytes, first) == 44);

	memset(&arena, 0, sizeof(arena));
	first = add_record(&arena, "first", 11, 1);
	add_record(&arena, TARGET, 22, 2);
	add_record(&arena, TARGET, 55, 3);
	assert(rk_filter_dirents64(arena.bytes, arena.length, TARGET,
				   sizeof(TARGET) - 1, &output) ==
	       RK_DIRENT_FILTER_CHANGED);
	assert(output == first);
	assert(cookie_at(arena.bytes, 0) == 55);
}

static void exact_name_and_unaligned(void)
{
	const char *names[] = {
		"rootkitlings_hidde", "rootkitlings_hidden_more",
		"xrootkitlings_hidden", "ROOTKITLINGS_HIDDEN",
	};
	unsigned char storage[4096];
	struct arena arena = {0};
	size_t output;
	size_t index;

	for (index = 0; index < sizeof(names) / sizeof(names[0]); index++)
		add_record(&arena, names[index], (int64_t)index + 1, 0x80 + index);
	expect_unchanged(&arena, RK_DIRENT_FILTER_UNCHANGED);
	add_record(&arena, TARGET, 99, 0x99);
	memset(storage, 0x6d, sizeof(storage));
	memcpy(storage + 1, arena.bytes, arena.length);
	assert(rk_filter_dirents64(storage + 1, arena.length, TARGET,
				   sizeof(TARGET) - 1, &output) ==
	       RK_DIRENT_FILTER_CHANGED);
	assert(storage[0] == 0x6d);
	assert(storage[arena.length + 1] == 0x6d);
}

static void expect_invalid(struct arena *arena)
{
	expect_unchanged(arena, RK_DIRENT_FILTER_INVALID);
}

static void invalid_cases(void)
{
	struct arena valid = {0};
	struct arena arena;
	char long_name[4097];
	unsigned short length;
	size_t cut;

	add_record(&valid, "valid", 1, 0x5a);
	for (cut = 1; cut <= NAME_OFFSET; cut++) {
		memset(&arena, 0, sizeof(arena));
		memcpy(arena.bytes, valid.bytes, cut);
		arena.length = cut;
		expect_invalid(&arena);
	}
	arena = valid;
	length = 0;
	memcpy(arena.bytes + offsetof(struct linux_dirent64, d_reclen),
	       &length, sizeof(length));
	expect_invalid(&arena);
	arena = valid;
	length = NAME_OFFSET;
	memcpy(arena.bytes + offsetof(struct linux_dirent64, d_reclen),
	       &length, sizeof(length));
	expect_invalid(&arena);
	arena = valid;
	length = (unsigned short)(valid.length + 8);
	memcpy(arena.bytes + offsetof(struct linux_dirent64, d_reclen),
	       &length, sizeof(length));
	expect_invalid(&arena);
	arena = valid;
	length = (unsigned short)(valid.length - 1);
	memcpy(arena.bytes + offsetof(struct linux_dirent64, d_reclen),
	       &length, sizeof(length));
	expect_invalid(&arena);
	arena = valid;
	memset(arena.bytes + NAME_OFFSET, 'x', arena.length - NAME_OFFSET);
	expect_invalid(&arena);
	memset(&arena, 0, sizeof(arena));
	add_record(&arena, "x", 1, 0);
	arena.bytes[NAME_OFFSET] = '\0';
	expect_invalid(&arena);
	memset(&arena, 0, sizeof(arena));
	add_record(&arena, "bad/name", 1, 0);
	expect_invalid(&arena);
	memset(&arena, 0, sizeof(arena));
	memset(long_name, 'p', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = '\0';
	add_record(&arena, long_name, 1, 0);
	expect_invalid(&arena);
	memset(&arena, 0, sizeof(arena));
	add_record(&arena, "abcdefgh", 1, 0xa7);
	arena.bytes[NAME_OFFSET + 1] = '\0';
	expect_invalid(&arena);

	memset(&arena, 0, sizeof(arena));
	add_record(&arena, TARGET, 1, 0x31);
	add_record(&arena, "valid", 2, 0x32);
	arena.bytes[arena.length - 1] = 0;
	arena.length--;
	expect_invalid(&arena);

	memset(&arena, 0, sizeof(arena));
	add_record(&arena, "valid", 1, 0x41);
	length = 0;
	memcpy(arena.bytes + offsetof(struct linux_dirent64, d_reclen),
	       &length, sizeof(length));
	add_record(&arena, TARGET, 2, 0x42);
	expect_invalid(&arena);
}

static uint32_t random_state = 0x13579bdfU;
static uint32_t next_random(void)
{
	random_state = random_state * 1664525U + 1013904223U;
	return random_state;
}

static void random_valid_cases(void)
{
	unsigned int run;

	for (run = 0; run < 2000; run++) {
		struct arena arena = {0};
		unsigned char original[sizeof(arena.bytes)];
		char expected_names[32][40];
		char target[40];
		int64_t source_cookies[32];
		size_t source_offsets[32];
		size_t source_lengths[32];
		bool removed[32];
		size_t count = 1 + next_random() % 32;
		size_t kept = 0;
		size_t output;
		size_t index;
		size_t offset;

		snprintf(target, sizeof(target), "target-%08x", next_random());
		for (index = 0; index < count; index++) {
			removed[index] = (next_random() % 4) == 0;
			if (removed[index])
				strcpy(expected_names[index], target);
			else {
				snprintf(expected_names[index], sizeof(expected_names[index]),
					 "entry-%zu-%08x", index, next_random());
				kept++;
			}
			source_cookies[index] = (int64_t)(1000 + index);
			source_offsets[index] = arena.length;
			source_lengths[index] = add_record(
				&arena, expected_names[index], source_cookies[index],
				(unsigned char)next_random());
		}
		memcpy(original, arena.bytes, arena.length);
		if (kept == 0 || kept == count) {
			size_t unchanged_output = SIZE_MAX;

			assert(rk_filter_dirents64(
				arena.bytes, arena.length, target, strlen(target),
				&unchanged_output) == RK_DIRENT_FILTER_UNCHANGED);
			assert(unchanged_output == arena.length);
			assert(memcmp(original, arena.bytes, arena.length) == 0);
			continue;
		}
		assert(rk_filter_dirents64(arena.bytes, arena.length, target,
					   strlen(target), &output) ==
		       RK_DIRENT_FILTER_CHANGED);
		offset = 0;
		for (index = 0; index < count; index++) {
			unsigned char expected_record[128];
			size_t next = index + 1;
			size_t record_length;
			int64_t expected_cookie;

			if (removed[index])
				continue;
			while (next < count && removed[next])
				next++;
			record_length = source_lengths[index];
			assert(record_length <= sizeof(expected_record));
			memcpy(expected_record, original + source_offsets[index],
			       record_length);
			expected_cookie = source_cookies[next - 1];
			memcpy(expected_record +
			       offsetof(struct linux_dirent64, d_off),
			       &expected_cookie, sizeof(expected_cookie));
			assert(reclen_at(arena.bytes, offset) == record_length);
			assert(memcmp(arena.bytes + offset, expected_record,
				      record_length) == 0);
			offset += record_length;
		}
		assert(offset == output);
		assert(output < arena.length);
		(void)original;
	}
}

int main(void)
{
	basic_cases();
	exact_name_and_unaligned();
	invalid_cases();
	random_valid_cases();
	puts("[+] packed dirent corpus: 2060 deterministic and random cases passed");
	return 0;
}
