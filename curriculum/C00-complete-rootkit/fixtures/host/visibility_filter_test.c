#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <linux/align.h>
#include <linux/dirent.h>

#include "dirent_filter.h"

#define CAPACITY 512U
#define NAME_OFFSET offsetof(struct linux_dirent64, d_name)

struct arena {
	unsigned char bytes[CAPACITY];
	size_t length;
};

static void append(struct arena *arena, const char *name, long long cookie)
{
	const size_t name_length = strlen(name);
	const size_t record_length = ALIGN(NAME_OFFSET + name_length + 1U,
					  sizeof(unsigned long long));
	unsigned char *record = arena->bytes + arena->length;
	unsigned short encoded_length = (unsigned short)record_length;

	assert(arena->length + record_length <= sizeof(arena->bytes));
	memset(record, 0, record_length);
	memcpy(record + offsetof(struct linux_dirent64, d_off), &cookie,
	       sizeof(cookie));
	memcpy(record + offsetof(struct linux_dirent64, d_reclen),
	       &encoded_length, sizeof(encoded_length));
	memcpy(record + NAME_OFFSET, name, name_length + 1U);
	arena->length += record_length;
}

static bool selected(const char *name, size_t length, const void *context)
{
	const char *const *names = context;
	size_t index;

	for (index = 0; names[index]; index++) {
		if (strlen(names[index]) == length &&
		    memcmp(name, names[index], length) == 0)
			return true;
	}
	return false;
}

static const char *name_at(const struct arena *arena, size_t wanted)
{
	size_t offset = 0;
	size_t index = 0;

	while (offset < arena->length) {
		const unsigned char *record = arena->bytes + offset;
		unsigned short length;

		if (index == wanted)
			return (const char *)record + NAME_OFFSET;
		memcpy(&length,
		       record + offsetof(struct linux_dirent64, d_reclen),
		       sizeof(length));
		offset += length;
		index++;
	}
	return NULL;
}

static long long cookie_at(const struct arena *arena, size_t wanted)
{
	const char *name = name_at(arena, wanted);
	long long cookie = 0;

	assert(name);
	memcpy(&cookie,
	       (const unsigned char *)name - NAME_OFFSET +
	       offsetof(struct linux_dirent64, d_off),
	       sizeof(cookie));
	return cookie;
}

static void multiple_targets(void)
{
	static const char *const names[] = {
		"rootkitlings",
		"rootkitlings_events",
		NULL,
	};
	struct arena arena = { 0 };
	size_t output;

	append(&arena, "alpha", 10);
	append(&arena, "rootkitlings", 20);
	append(&arena, "rootkitlings_events", 30);
	append(&arena, "omega", 40);
	assert(rk_filter_dirents64_if(arena.bytes, arena.length, selected,
				      names, &output) ==
	       RK_DIRENT_FILTER_CHANGED);
	arena.length = output;
	assert(strcmp(name_at(&arena, 0), "alpha") == 0);
	assert(strcmp(name_at(&arena, 1), "omega") == 0);
	assert(name_at(&arena, 2) == NULL);
	assert(cookie_at(&arena, 0) == 30);
}

static void unchanged_boundaries(void)
{
	static const char *const selected_names[] = {
		"rootkitlings",
		"rootkitlings_events",
		NULL,
	};
	struct arena all_match = { 0 };
	struct arena no_match = { 0 };
	struct arena original;
	size_t output;

	append(&all_match, "rootkitlings", 10);
	append(&all_match, "rootkitlings_events", 20);
	original = all_match;
	assert(rk_filter_dirents64_if(
		all_match.bytes, all_match.length, selected, selected_names,
		&output) == RK_DIRENT_FILTER_UNCHANGED);
	assert(output == original.length);
	assert(memcmp(&all_match, &original, sizeof(original)) == 0);

	append(&no_match, "alpha", 10);
	append(&no_match, "omega", 20);
	original = no_match;
	assert(rk_filter_dirents64_if(
		no_match.bytes, no_match.length, selected, selected_names,
		&output) == RK_DIRENT_FILTER_UNCHANGED);
	assert(output == original.length);
	assert(memcmp(&no_match, &original, sizeof(original)) == 0);
}

static void invalid_input_is_unchanged(void)
{
	static const char *const names[] = { "rootkitlings", NULL };
	struct arena arena = { 0 };
	struct arena original;
	unsigned short invalid = 1;
	size_t output = 0;

	append(&arena, "rootkitlings", 10);
	memcpy(arena.bytes + offsetof(struct linux_dirent64, d_reclen),
	       &invalid, sizeof(invalid));
	original = arena;
	assert(rk_filter_dirents64_if(arena.bytes, arena.length, selected,
				      names, &output) ==
	       RK_DIRENT_FILTER_INVALID);
	assert(output == arena.length);
	assert(memcmp(&arena, &original, sizeof(original)) == 0);
}

static void exact_wrapper(void)
{
	struct arena arena = { 0 };
	size_t output;

	append(&arena, "alpha", 10);
	append(&arena, "rootkitlings", 20);
	append(&arena, "omega", 30);
	assert(rk_filter_dirents64(
		arena.bytes, arena.length, "rootkitlings", 12, &output) ==
	       RK_DIRENT_FILTER_CHANGED);
	arena.length = output;
	assert(strcmp(name_at(&arena, 0), "alpha") == 0);
	assert(strcmp(name_at(&arena, 1), "omega") == 0);
}

int main(void)
{
	multiple_targets();
	unchanged_boundaries();
	invalid_input_is_unchanged();
	exact_wrapper();
	puts("[+] multi-name directory filtering verified");
	return 0;
}
