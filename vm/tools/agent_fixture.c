// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
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

static void write_file(const char *path, const char *value)
{
	FILE *stream = fopen(path, "w");

	if (!stream)
		fail("fopen fixture");
	if (fputs(value, stream) == EOF || fclose(stream) != 0)
		fail("write fixture");
}

static void write_marker(const char *directory, const char *name)
{
	char path[512];

	if (snprintf(path, sizeof(path), "%s/%s", directory, name) >=
	    (int)sizeof(path)) {
		errno = ENAMETOOLONG;
		fail("fixture path");
	}
	write_file(path, "1\n");
}

static int open_listener(unsigned short requested, unsigned short *actual,
			 bool listen_now)
{
	struct sockaddr_in address = {
	    .sin_family = AF_INET,
	    .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	    .sin_port = htons(requested),
	};
	socklen_t length = sizeof(address);
	int reuse = 1;
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);

	if (fd < 0)
		fail("socket");
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
		0 ||
	    bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
	    getsockname(fd, (struct sockaddr *)&address, &length) < 0)
		fail("listener setup");
	if (listen_now && listen(fd, 2) < 0)
		fail("listen");
	*actual = ntohs(address.sin_port);
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

static void send_frame(int fd, const char *payload)
{
	size_t length = strlen(payload);
	unsigned char header[2] = {length >> 8, length};
	size_t index;

	write_all(fd, header, 1);
	write_all(fd, header + 1, 1);
	for (index = 0; index < length; index++)
		write_all(fd, (const unsigned char *)payload + index, 1);
}

static void send_oversized(int fd)
{
	unsigned char bytes[67] = {0, 65};

	memset(bytes + 2, 'x', 65);
	write_all(fd, bytes, sizeof(bytes));
}

static void expect_frame(int fd, const char *expected)
{
	unsigned char payload[64];
	unsigned char header[2];
	size_t declared;
	size_t length = strlen(expected);

	read_all(fd, header, sizeof(header));
	declared = ((size_t)header[0] << 8) | header[1];
	if (declared != length || declared > sizeof(payload)) {
		errno = EPROTO;
		fail("reply length");
	}
	read_all(fd, payload, declared);
	if (memcmp(payload, expected, length)) {
		errno = EPROTO;
		fail("reply payload");
	}
}

static void expect_eof(int fd)
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

static int reserve(const char *path)
{
	char text[32];
	unsigned short port;
	int fd = open_listener(0, &port, false);

	close(fd);
	if (snprintf(text, sizeof(text), "%u\n", port) >= (int)sizeof(text)) {
		errno = EOVERFLOW;
		fail("port text");
	}
	write_file(path, text);
	return 0;
}

static int serve(const char *directory, const char *port_text)
{
	struct timeval timeout = {.tv_sec = 10};
	unsigned long parsed = strtoul(port_text, NULL, 10);
	unsigned short port;
	int listener;
	int peer;

	if (!parsed || parsed > 65535) {
		errno = EINVAL;
		fail("port");
	}
	listener = open_listener((unsigned short)parsed, &port, true);
	if (port != parsed) {
		errno = EPROTO;
		fail("bound port");
	}
	write_marker(directory, "ready");

	peer = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
	if (peer < 0)
		fail("accept first");
	if (setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout,
		       sizeof(timeout)) < 0)
		fail("first timeout");
	send_frame(peer, "PING");
	expect_frame(peer, "PONG");
	send_frame(peer, "INFO");
	expect_frame(peer, "INFO rklings");
	send_frame(peer, "UNKNOWN");
	expect_frame(peer, "ERR");
	send_oversized(peer);
	expect_frame(peer, "ERR");
	send_frame(peer, "QUIT");
	expect_eof(peer);
	close(peer);
	write_marker(directory, "first_complete");

	peer = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
	if (peer < 0)
		fail("accept second");
	if (setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout,
		       sizeof(timeout)) < 0)
		fail("second timeout");
	send_frame(peer, "PING");
	expect_frame(peer, "PONG");
	write_marker(directory, "second_connected");
	expect_eof(peer);
	write_marker(directory, "eof");
	close(peer);
	close(listener);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "reserve"))
		return reserve(argv[2]);
	if (argc == 4 && !strcmp(argv[1], "serve"))
		return serve(argv[2], argv[3]);
	fprintf(stderr, "usage: %s reserve FILE | serve DIRECTORY PORT\n",
		argv[0]);
	return 2;
}
