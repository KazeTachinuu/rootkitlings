#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dns_name.h"

static unsigned long assertions;

#define CHECK(condition) do { \
	assertions++; \
	if (!(condition)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
			__FILE__, __LINE__, #condition); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static int all_bytes_are(const unsigned char *bytes, size_t length,
			 unsigned char value)
{
	size_t index;

	for (index = 0; index < length; index++)
		if (bytes[index] != value)
			return 0;
	return 1;
}

static void expect_parse_failure(const unsigned char *wire, size_t available,
				 enum rk_dns_name_result expected)
{
	struct rk_dns_name_view view;
	struct rk_dns_name_view before;

	memset(&view, 0xa5, sizeof(view));
	before = view;
	CHECK(rk_dns_name_parse(wire, available, &view) == expected);
	CHECK(memcmp(&view, &before, sizeof(view)) == 0);
}

static void expect_encode_failure(const struct rk_dns_label *labels,
				  size_t label_count, size_t capacity,
				  enum rk_dns_name_result expected)
{
	unsigned char wire[RK_DNS_NAME_MAX + 32];
	unsigned char before[sizeof(wire)];
	size_t used = 0xa5a5;

	memset(wire, 0x5a, sizeof(wire));
	memcpy(before, wire, sizeof(wire));
	CHECK(rk_dns_name_encode(labels, label_count, wire, capacity, &used) ==
	      expected);
	CHECK(used == 0xa5a5);
	CHECK(memcmp(wire, before, sizeof(wire)) == 0);
}

static size_t make_prefix(unsigned char *wire, size_t target)
{
	size_t offset = 0;

	CHECK(target != 1);
	while (offset < target) {
		size_t remaining = target - offset;
		size_t span;

		if (remaining <= 64)
			span = remaining;
		else if (remaining - 64 == 1)
			span = 63;
		else
			span = 64;
		CHECK(span >= 2 && span <= 64);
		wire[offset++] = (unsigned char)(span - 1);
		memset(wire + offset, 0x6b, span - 1);
		offset += span - 1;
	}
	CHECK(offset == target);
	return offset;
}

static void test_arguments(void)
{
	unsigned char wire[8];
	struct rk_dns_name_view view;
	struct rk_dns_name_view before;
	struct rk_dns_label label = { NULL, 1 };
	struct rk_dns_label null_empty = { NULL, 0 };
	size_t used = 77;

	memset(wire, 0x5a, sizeof(wire));
	memset(&view, 0xa5, sizeof(view));
	before = view;
	CHECK(rk_dns_name_encode(NULL, 0, NULL, 0, &used) ==
	      RK_DNS_NAME_INVALID_ARGUMENT);
	CHECK(used == 77);
	CHECK(rk_dns_name_encode(NULL, 0, wire, sizeof(wire), NULL) ==
	      RK_DNS_NAME_INVALID_ARGUMENT);
	CHECK(rk_dns_name_encode(NULL, 1, wire, sizeof(wire), &used) ==
	      RK_DNS_NAME_INVALID_ARGUMENT);
	CHECK(rk_dns_name_encode(&label, 1, wire, sizeof(wire), &used) ==
	      RK_DNS_NAME_INVALID_ARGUMENT);
	CHECK(rk_dns_name_encode(&null_empty, 1, wire, 0, &used) ==
	      RK_DNS_NAME_INVALID_ARGUMENT);
	CHECK(used == 77 && all_bytes_are(wire, sizeof(wire), 0x5a));

	CHECK(rk_dns_name_parse(NULL, 0, &view) ==
	      RK_DNS_NAME_INVALID_ARGUMENT);
	CHECK(memcmp(&view, &before, sizeof(view)) == 0);
	CHECK(rk_dns_name_parse(wire, sizeof(wire), NULL) ==
	      RK_DNS_NAME_INVALID_ARGUMENT);
}

static void test_root_and_suffix(void)
{
	unsigned char wire[8];
	struct rk_dns_name_view view;
	size_t used = 99;

	memset(wire, 0x5a, sizeof(wire));
	CHECK(rk_dns_name_encode(NULL, 0, wire, sizeof(wire), &used) ==
	      RK_DNS_NAME_OK);
	CHECK(used == 1 && wire[0] == 0 && wire[1] == 0x5a);
	wire[1] = 0xff;
	wire[2] = 0xc0;
	CHECK(rk_dns_name_parse(wire, sizeof(wire), &view) == RK_DNS_NAME_OK);
	CHECK(view.label_count == 0 && view.consumed == 1);
}

static void test_first_octets(void)
{
	unsigned int first;
	unsigned char wire[64] = { 0 };
	struct rk_dns_name_view view;

	for (first = 0; first <= 255; first++) {
		enum rk_dns_name_result expected;

		wire[0] = (unsigned char)first;
		if (first == 0)
			expected = RK_DNS_NAME_OK;
		else if (first <= RK_DNS_LABEL_MAX)
			expected = RK_DNS_NAME_TRUNCATED;
		else if (first < 0xc0)
			expected = RK_DNS_NAME_MALFORMED;
		else
			expected = RK_DNS_NAME_UNSUPPORTED_COMPRESSION;

		if (expected == RK_DNS_NAME_OK) {
			memset(&view, 0xa5, sizeof(view));
			CHECK(rk_dns_name_parse(wire, 1, &view) == expected);
			CHECK(view.label_count == 0 && view.consumed == 1);
		} else {
			expect_parse_failure(wire, 1, expected);
		}
	}
	wire[0] = 0xc0;
	expect_parse_failure(wire, 1, RK_DNS_NAME_UNSUPPORTED_COMPRESSION);
}

static void test_every_label_length(void)
{
	unsigned char data[RK_DNS_LABEL_MAX];
	unsigned char wire[RK_DNS_LABEL_MAX + 2];
	struct rk_dns_name_view view;
	struct rk_dns_label label;
	size_t length;
	size_t used;

	for (length = 0; length < sizeof(data); length++)
		data[length] = (unsigned char)(length * 17);
	label.data = data;
	for (length = 1; length <= RK_DNS_LABEL_MAX; length++) {
		label.length = length;
		memset(wire, 0xa5, sizeof(wire));
		used = 0;
		CHECK(rk_dns_name_encode(&label, 1, wire, sizeof(wire), &used) ==
		      RK_DNS_NAME_OK);
		CHECK(used == length + 2 && wire[0] == length &&
		      wire[length + 1] == 0);
		CHECK(memcmp(wire + 1, data, length) == 0);
		CHECK(rk_dns_name_parse(wire, used, &view) == RK_DNS_NAME_OK);
		CHECK(view.label_count == 1 && view.consumed == used &&
		      view.data_offsets[0] == 1 && view.lengths[0] == length);
	}
}

static size_t encode_max_four(unsigned char *wire)
{
	unsigned char data[4][RK_DNS_LABEL_MAX];
	struct rk_dns_label labels[4];
	size_t used = 0;
	size_t index;

	for (index = 0; index < 4; index++) {
		memset(data[index], (int)(0x20 + index), sizeof(data[index]));
		labels[index].data = data[index];
		labels[index].length = index == 3 ? 61 : 63;
	}
	CHECK(rk_dns_name_encode(labels, 4, wire, RK_DNS_NAME_MAX, &used) ==
	      RK_DNS_NAME_OK);
	CHECK(used == RK_DNS_NAME_MAX);
	return used;
}

static void test_maximum_names_and_truncation(void)
{
	unsigned char wire[RK_DNS_NAME_MAX];
	unsigned char one = 0x2e;
	struct rk_dns_label labels[RK_DNS_LABELS_MAX];
	struct rk_dns_name_view view;
	size_t available;
	size_t used;
	size_t index;

	used = encode_max_four(wire);
	for (available = 0; available < used; available++)
		expect_parse_failure(wire, available, RK_DNS_NAME_TRUNCATED);
	CHECK(rk_dns_name_parse(wire, used, &view) == RK_DNS_NAME_OK);
	CHECK(view.label_count == 4 && view.consumed == RK_DNS_NAME_MAX);

	for (index = 0; index < RK_DNS_LABELS_MAX; index++) {
		labels[index].data = &one;
		labels[index].length = 1;
	}
	CHECK(rk_dns_name_encode(labels, RK_DNS_LABELS_MAX, wire,
				 RK_DNS_NAME_MAX, &used) == RK_DNS_NAME_OK);
	CHECK(used == RK_DNS_NAME_MAX);
	CHECK(rk_dns_name_parse(wire, used, &view) == RK_DNS_NAME_OK);
	CHECK(view.label_count == RK_DNS_LABELS_MAX &&
	      view.consumed == RK_DNS_NAME_MAX);
	for (index = 0; index < RK_DNS_LABELS_MAX; index++)
		CHECK(view.data_offsets[index] == index * 2 + 1 &&
		      view.lengths[index] == 1);
}

static void test_capacities_and_encode_failures(void)
{
	unsigned char first[63];
	unsigned char second[63];
	unsigned char third[63];
	unsigned char fourth[62];
	struct rk_dns_label valid[4] = {
		{ first, 63 }, { second, 63 }, { third, 63 }, { fourth, 61 },
	};
	struct rk_dns_label too_long[4] = {
		{ first, 63 }, { second, 63 }, { third, 63 }, { fourth, 62 },
	};
	struct rk_dns_label empty = { first, 0 };
	struct rk_dns_label over_label = { first, 64 };
	struct rk_dns_label too_many[RK_DNS_LABELS_MAX + 1];
	size_t capacity;
	size_t index;

	memset(first, 1, sizeof(first));
	memset(second, 2, sizeof(second));
	memset(third, 3, sizeof(third));
	memset(fourth, 4, sizeof(fourth));
	for (capacity = 0; capacity < RK_DNS_NAME_MAX; capacity++)
		expect_encode_failure(valid, 4, capacity, RK_DNS_NAME_NO_SPACE);
	expect_encode_failure(&empty, 1, 0, RK_DNS_NAME_MALFORMED);
	expect_encode_failure(&over_label, 1, 0, RK_DNS_NAME_MALFORMED);
	expect_encode_failure(too_long, 4, 0, RK_DNS_NAME_MALFORMED);

	for (index = 0; index < RK_DNS_LABELS_MAX + 1; index++) {
		too_many[index].data = first;
		too_many[index].length = 1;
	}
	expect_encode_failure(too_many, RK_DNS_LABELS_MAX + 1,
			      sizeof(first), RK_DNS_NAME_MALFORMED);
	too_many[0].data = NULL;
	expect_encode_failure(too_many, 1, 0, RK_DNS_NAME_INVALID_ARGUMENT);
}

static void test_binary_unaligned_and_overlap(void)
{
	unsigned char storage[320];
	unsigned char binary[] = { 0x00, 0x2e, 0xff, 0x41 };
	struct rk_dns_label label = { binary, sizeof(binary) };
	struct rk_dns_label overlap[2];
	struct rk_dns_name_view view;
	size_t used;

	memset(storage, 0xa5, sizeof(storage));
	CHECK(rk_dns_name_encode(&label, 1, storage + 1,
				 sizeof(storage) - 1, &used) == RK_DNS_NAME_OK);
	CHECK(used == 6 && storage[0] == 0xa5 && storage[1] == 4 &&
	      !memcmp(storage + 2, binary, sizeof(binary)) && storage[6] == 0);
	CHECK(rk_dns_name_parse(storage + 1, used, &view) == RK_DNS_NAME_OK);
	CHECK(view.label_count == 1 && view.data_offsets[0] == 1 &&
	      view.lengths[0] == sizeof(binary));

	memset(storage, 0xcc, sizeof(storage));
	memcpy(storage + 100, "first", 5);
	memcpy(storage + 2, "second", 6);
	overlap[0].data = storage + 100;
	overlap[0].length = 5;
	overlap[1].data = storage + 2;
	overlap[1].length = 6;
	CHECK(rk_dns_name_encode(overlap, 2, storage, sizeof(storage), &used) ==
	      RK_DNS_NAME_OK);
	CHECK(used == 14 && storage[0] == 5 &&
	      !memcmp(storage + 1, "first", 5) && storage[6] == 6 &&
	      !memcmp(storage + 7, "second", 6) && storage[13] == 0);
}

static void test_every_label_pair(void)
{
	unsigned char first[RK_DNS_LABEL_MAX];
	unsigned char second[RK_DNS_LABEL_MAX];
	unsigned char wire[2 * RK_DNS_LABEL_MAX + 3];
	struct rk_dns_label labels[2] = {
		{ first, 1 }, { second, 1 },
	};
	struct rk_dns_name_view view;
	size_t first_length;
	size_t second_length;
	size_t used;

	memset(first, 0x00, sizeof(first));
	memset(second, 0xff, sizeof(second));
	for (first_length = 1; first_length <= RK_DNS_LABEL_MAX;
	     first_length++) {
		labels[0].length = first_length;
		for (second_length = 1; second_length <= RK_DNS_LABEL_MAX;
		     second_length++) {
			labels[1].length = second_length;
			used = 0;
			CHECK(rk_dns_name_encode(labels, 2, wire, sizeof(wire),
						 &used) == RK_DNS_NAME_OK);
			CHECK(used == first_length + second_length + 3);
			CHECK(rk_dns_name_parse(wire, used, &view) ==
			      RK_DNS_NAME_OK);
			CHECK(view.label_count == 2 && view.consumed == used &&
			      view.data_offsets[0] == 1 &&
			      view.lengths[0] == first_length &&
			      view.data_offsets[1] == first_length + 2 &&
			      view.lengths[1] == second_length);
		}
	}
}

static void test_type_markers_at_boundaries(void)
{
	unsigned char wire[RK_DNS_NAME_MAX + 2];
	size_t position;

	for (position = 0; position <= 253; position++) {
		if (position == 1)
			continue;
		memset(wire, 0, sizeof(wire));
		make_prefix(wire, position);
		wire[position] = 0x40;
		expect_parse_failure(wire, position + 1, RK_DNS_NAME_MALFORMED);
		wire[position] = 0x80;
		expect_parse_failure(wire, position + 1, RK_DNS_NAME_MALFORMED);
		wire[position] = 0xc0;
		expect_parse_failure(wire, position + 1,
				     RK_DNS_NAME_UNSUPPORTED_COMPRESSION);
		wire[position] = 0xff;
		expect_parse_failure(wire, position + 1,
				     RK_DNS_NAME_UNSUPPORTED_COMPRESSION);
	}
}

static void test_missing_root_and_precedence(void)
{
	unsigned char wire[RK_DNS_NAME_MAX + 1];
	struct rk_dns_label label;
	unsigned char data[63];

	wire[0] = 1;
	wire[1] = 'x';
	expect_parse_failure(wire, 2, RK_DNS_NAME_TRUNCATED);

	make_prefix(wire, RK_DNS_NAME_MAX);
	expect_parse_failure(wire, RK_DNS_NAME_MAX, RK_DNS_NAME_MALFORMED);

	make_prefix(wire, 253);
	wire[253] = 1;
	expect_parse_failure(wire, 254, RK_DNS_NAME_MALFORMED);

	make_prefix(wire, 254);
	expect_parse_failure(wire, 254, RK_DNS_NAME_TRUNCATED);
	wire[254] = 0xc0;
	expect_parse_failure(wire, 255, RK_DNS_NAME_UNSUPPORTED_COMPRESSION);

	memset(data, 0x33, sizeof(data));
	label.data = data;
	label.length = 64;
	expect_encode_failure(&label, 1, 0, RK_DNS_NAME_MALFORMED);
}

int main(void)
{
	test_arguments();
	test_root_and_suffix();
	test_first_octets();
	test_every_label_length();
	test_maximum_names_and_truncation();
	test_capacities_and_encode_failures();
	test_binary_unaligned_and_overlap();
	test_every_label_pair();
	test_type_markers_at_boundaries();
	test_missing_root_and_precedence();

	printf("[+] bounded DNS wire-name codec: %lu assertions\n", assertions);
	return EXIT_SUCCESS;
}
