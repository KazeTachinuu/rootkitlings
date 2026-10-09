// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static void fail(const char *operation)
{
	perror(operation);
	exit(EXIT_FAILURE);
}

static void write_number(const char *directory, const char *name,
			 unsigned int value)
{
	char path[512];
	FILE *stream;

	if (snprintf(path, sizeof(path), "%s/%s", directory, name) >=
	    (int)sizeof(path)) {
		errno = ENAMETOOLONG;
		fail("fixture path");
	}
	stream = fopen(path, "w");
	if (!stream)
		fail("fopen fixture");
	if (fprintf(stream, "%u\n", value) < 0 || fclose(stream) != 0)
		fail("write fixture");
}

static int listener(unsigned short *port)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	socklen_t length = sizeof(address);
	int fd;

	fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
	if (fd < 0)
		fail("socket");
	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
	    getsockname(fd, (struct sockaddr *)&address, &length) < 0 ||
	    listen(fd, 1) < 0)
		fail("listener setup");
	if (length != sizeof(address) || address.sin_family != AF_INET) {
		errno = EPROTO;
		fail("listener address");
	}
	*port = ntohs(address.sin_port);
	return fd;
}

static void write_all(int fd, const unsigned char *bytes, size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		ssize_t result = write(fd, bytes + offset, length - offset);

		if (result < 0 && errno == EINTR)
			continue;
		if (result <= 0)
			fail("write stream");
		offset += (size_t)result;
	}
}

static void read_all(int fd, unsigned char *bytes, size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		ssize_t result = read(fd, bytes + offset, length - offset);

		if (result < 0 && errno == EINTR)
			continue;
		if (result <= 0) {
			if (result == 0)
				errno = ECONNRESET;
			fail("read stream");
		}
		offset += (size_t)result;
	}
}

static void send_frame_split(int fd, const unsigned char *payload,
			     size_t length)
{
	unsigned char header[2] = { length >> 8, length };
	size_t index;

	write_all(fd, header, 1);
	write_all(fd, header + 1, 1);
	for (index = 0; index < length; index++)
		write_all(fd, payload + index, 1);
}

static void expect_frame(int fd, const unsigned char *payload, size_t length)
{
	unsigned char received[64];
	unsigned char header[2];
	size_t declared;

	read_all(fd, header, sizeof(header));
	declared = ((size_t)header[0] << 8) | header[1];
	if (declared != length || declared > sizeof(received)) {
		errno = EPROTO;
		fail("echo length");
	}
	read_all(fd, received, declared);
	if (length && memcmp(received, payload, length)) {
		errno = EPROTO;
		fail("echo payload");
	}
}

static void send_oversized_then_valid(int fd, size_t oversized,
				       const unsigned char *valid,
				       size_t valid_length)
{
	size_t total = 2 + oversized + 2 + valid_length;
	unsigned char *stream = malloc(total);

	if (!stream)
		fail("malloc oversized frame");
	stream[0] = oversized >> 8;
	stream[1] = oversized;
	memset(stream + 2, 0x6d, oversized);
	stream[2 + oversized] = valid_length >> 8;
	stream[3 + oversized] = valid_length;
	memcpy(stream + 4 + oversized, valid, valid_length);
	write_all(fd, stream, total);
	free(stream);
	expect_frame(fd, valid, valid_length);
}

static void wait_for_eof(int fd)
{
	unsigned char byte;
	ssize_t result;

	do {
		result = read(fd, &byte, 1);
	} while (result < 0 && errno == EINTR);
	if (result != 0) {
		if (result > 0)
			errno = EPROTO;
		fail("session EOF");
	}
}

static int serve(const char *mode, const char *directory)
{
	struct timeval timeout = { .tv_sec = 8 };
	unsigned char binary[64];
	unsigned char coalesced[] = {
		0, 3, 0xa0, 0x00, 0xa1,
		0, 2, 0xff, 0x00,
	};
	const unsigned char after_65[] = { 0x65 };
	const unsigned char after_65535[] = { 0x00, 0xff, 0x23, 0x80 };
	unsigned short port;
	int listener_fd;
	int peer;
	size_t index;

	listener_fd = listener(&port);
	write_number(directory, "port", port);
	write_number(directory, "ready", 1);
	peer = accept4(listener_fd, NULL, NULL, SOCK_CLOEXEC);
	if (peer < 0)
		fail("accept");
	if (setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout,
		       sizeof(timeout)) < 0)
		fail("setsockopt timeout");
	write_number(directory, "accepted", 1);

	if (!strcmp(mode, "serve")) {
		for (index = 0; index < sizeof(binary); index++)
			binary[index] = (unsigned char)(index * 43U);
		send_frame_split(peer, NULL, 0);
		expect_frame(peer, NULL, 0);
		send_frame_split(peer, binary, sizeof(binary));
		expect_frame(peer, binary, sizeof(binary));
		write_all(peer, coalesced, sizeof(coalesced));
		expect_frame(peer, coalesced + 2, 3);
		expect_frame(peer, coalesced + 7, 2);
		send_oversized_then_valid(peer, 65, after_65,
					   sizeof(after_65));
		send_oversized_then_valid(peer, 65535, after_65535,
					   sizeof(after_65535));
		write_number(directory, "complete", 1);
	}

	wait_for_eof(peer);
	write_number(directory, "eof", 1);
	close(peer);
	close(listener_fd);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && (!strcmp(argv[1], "serve") ||
			  !strcmp(argv[1], "hold")))
		return serve(argv[1], argv[2]);
	fprintf(stderr, "usage: %s serve|hold DIRECTORY\n", argv[0]);
	return 2;
}
