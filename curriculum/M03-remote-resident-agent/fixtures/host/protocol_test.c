#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol.h"

static unsigned long assertions;

#define CHECK(condition) do { \
	assertions++; \
	if (!(condition)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
			__FILE__, __LINE__, #condition); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static void expect_kind(const unsigned char *bytes, size_t length,
			enum rk_command_kind kind)
{
	struct rk_command command;

	memset(&command, 0xa5, sizeof(command));
	CHECK(rk_command_parse(bytes, length, &command) == 0);
	CHECK(command.kind == kind);
}

static void expect_rejected(const unsigned char *bytes, size_t length)
{
	struct rk_command command;
	struct rk_command before;

	memset(&command, 0xa5, sizeof(command));
	before = command;
	CHECK(rk_command_parse(bytes, length, &command) == -EINVAL);
	CHECK(memcmp(&command, &before, sizeof(command)) == 0);
}

static int marker_byte(unsigned int byte)
{
	return (byte >= 'a' && byte <= 'z') ||
	       (byte >= 'A' && byte <= 'Z') ||
	       (byte >= '0' && byte <= '9') || byte == '-' || byte == '_';
}

static void test_fixed_commands(void)
{
	static const unsigned char ping[] = "PING";
	static const unsigned char info[] = "INFO";
	static const unsigned char quit[] = "QUIT";
	static const unsigned char unknown[] = "PONG";
	size_t length;

	expect_kind(ping, sizeof(ping) - 1, RK_COMMAND_PING);
	expect_kind(info, sizeof(info) - 1, RK_COMMAND_INFO);
	expect_kind(quit, sizeof(quit) - 1, RK_COMMAND_QUIT);
	for (length = 0; length < sizeof(ping) - 1; length++)
		expect_rejected(ping, length);
	expect_rejected(unknown, sizeof(unknown) - 1);
	expect_rejected((const unsigned char *)"PING\n", 5);
	expect_rejected((const unsigned char *)"ping", 4);
	CHECK(rk_command_parse(NULL, 0, &(struct rk_command){ 0 }) == -EINVAL);
	CHECK(rk_command_parse(ping, sizeof(ping) - 1, NULL) == -EINVAL);
}

static void test_markers(void)
{
	unsigned char frame[5 + RK_MARKER_MAX + 1] = "MARK ";
	struct rk_command command;
	size_t length;
	unsigned int byte;

	for (length = 1; length <= RK_MARKER_MAX; length++) {
		memset(frame + 5, 'a', length);
		CHECK(rk_command_parse(frame, 5 + length, &command) == 0);
		CHECK(command.kind == RK_COMMAND_MARK);
		CHECK(command.marker_length == length);
		CHECK(command.marker[length] == '\0');
		CHECK(memcmp(command.marker, frame + 5, length) == 0);
	}
	expect_rejected((const unsigned char *)"MARK ", 5);
	memset(frame + 5, 'a', RK_MARKER_MAX + 1);
	expect_rejected(frame, sizeof(frame));

	for (byte = 0; byte <= 255; byte++) {
		frame[5] = (unsigned char)byte;
		if (marker_byte(byte)) {
			CHECK(rk_command_parse(frame, 6, &command) == 0);
			CHECK(command.marker[0] == (char)byte);
		} else {
			expect_rejected(frame, 6);
		}
	}
}

static void test_retry_bounds(void)
{
	CHECK(rk_retry_next(0, 25, 200) == 25);
	CHECK(rk_retry_next(25, 25, 200) == 50);
	CHECK(rk_retry_next(50, 25, 200) == 100);
	CHECK(rk_retry_next(100, 25, 200) == 200);
	CHECK(rk_retry_next(200, 25, 200) == 200);
	CHECK(rk_retry_next(199, 25, 200) == 200);
	CHECK(rk_retry_next(1, 0, 200) == 0);
	CHECK(rk_retry_next(1, 201, 200) == 0);
	CHECK(rk_retry_next(~0U, 1, ~0U) == ~0U);
}

int main(void)
{
	test_fixed_commands();
	test_markers();
	test_retry_bounds();
	printf("[+] M03 protocol and retry policy: %lu assertions\n", assertions);
	return EXIT_SUCCESS;
}
