// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void fail(const char *operation)
{
	perror(operation);
	exit(1);
}

static int loopback_listener(unsigned short *port)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	socklen_t length = sizeof(address);
	int descriptor;

	descriptor = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
	if (descriptor < 0)
		fail("socket");
	if (bind(descriptor, (struct sockaddr *)&address, sizeof(address)) < 0)
		fail("bind");
	if (getsockname(descriptor, (struct sockaddr *)&address, &length) < 0)
		fail("getsockname");
	if (length != sizeof(address) || address.sin_family != AF_INET)
		fail("unexpected listener address");
	if (listen(descriptor, 4) < 0)
		fail("listen");
	*port = ntohs(address.sin_port);
	return descriptor;
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
		fail("fopen");
	if (fprintf(stream, "%u\n", value) < 0 || fclose(stream) != 0)
		fail("write fixture value");
}

static int connect_loopback(unsigned short port, unsigned short *local_port)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
		.sin_port = htons(port),
	};
	socklen_t length = sizeof(address);
	int descriptor;

	descriptor = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
	if (descriptor < 0)
		fail("socket client");
	if (connect(descriptor, (struct sockaddr *)&address, sizeof(address)) < 0)
		fail("connect");
	memset(&address, 0, sizeof(address));
	if (getsockname(descriptor, (struct sockaddr *)&address, &length) < 0)
		fail("client getsockname");
	*local_port = ntohs(address.sin_port);
	return descriptor;
}

static int serve(const char *directory)
{
	unsigned short primary_port;
	unsigned short unrelated_server_port;
	unsigned short unrelated_client_port;
	char byte;
	ssize_t count;
	int primary_listener;
	int primary_peer;
	int unrelated_listener;
	int unrelated_client;
	int unrelated_peer;

	primary_listener = loopback_listener(&primary_port);
	unrelated_listener = loopback_listener(&unrelated_server_port);
	unrelated_client = connect_loopback(unrelated_server_port,
					    &unrelated_client_port);
	unrelated_peer = accept4(unrelated_listener, NULL, NULL, SOCK_CLOEXEC);
	if (unrelated_peer < 0)
		fail("accept unrelated");

	write_number(directory, "primary_port", primary_port);
	write_number(directory, "unrelated_local_port", unrelated_client_port);
	write_number(directory, "unrelated_peer_port", unrelated_server_port);
	write_number(directory, "ready", 1);

	primary_peer = accept4(primary_listener, NULL, NULL, SOCK_CLOEXEC);
	if (primary_peer < 0)
		fail("accept primary");
	write_number(directory, "accepted", 1);

	do {
		count = read(primary_peer, &byte, sizeof(byte));
	} while (count < 0 && errno == EINTR);
	if (count != 0) {
		if (count > 0)
			errno = EPROTO;
		fail("primary EOF");
	}
	write_number(directory, "eof", 1);

	close(primary_peer);
	close(primary_listener);
	close(unrelated_peer);
	close(unrelated_client);
	close(unrelated_listener);
	return 0;
}

static int unused(const char *path)
{
	unsigned short port;
	int descriptor;
	FILE *stream;

	descriptor = loopback_listener(&port);
	close(descriptor);
	stream = fopen(path, "w");
	if (!stream)
		fail("fopen unused port");
	if (fprintf(stream, "%u\n", port) < 0 || fclose(stream) != 0)
		fail("write unused port");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "serve") == 0)
		return serve(argv[2]);
	if (argc == 3 && strcmp(argv[1], "unused") == 0)
		return unused(argv[2]);
	fprintf(stderr, "usage: %s serve DIRECTORY | unused PORT_FILE\n", argv[0]);
	return 2;
}
