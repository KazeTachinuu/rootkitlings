// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <linux/udp.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef SOL_UDP
#define SOL_UDP 17
#endif

struct payload {
	uint32_t sequence;
	uint32_t inverse;
	unsigned char bytes[32];
};

static void fail(const char *message)
{
	perror(message);
	exit(EXIT_FAILURE);
}

static unsigned int parse_port(const char *text)
{
	char *end;
	unsigned long value;

	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno || !*text || *end || !value || value > 65535) {
		fprintf(stderr, "invalid port: %s\n", text);
		exit(EXIT_FAILURE);
	}
	return (unsigned int)value;
}

static struct sockaddr_in address(const char *ip, unsigned int port)
{
	struct sockaddr_in result;

	memset(&result, 0, sizeof(result));
	result.sin_family = AF_INET;
	result.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, ip, &result.sin_addr) != 1)
		fail("inet_pton");
	return result;
}

static int receiver(const char *ip, unsigned int port)
{
	struct sockaddr_in local = address(ip, port);
	struct timeval timeout = { .tv_sec = 3 };
	int fd = socket(AF_INET, SOCK_DGRAM, 0);

	if (fd < 0)
		fail("socket receiver");
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
		       sizeof(timeout)) < 0)
		fail("setsockopt timeout");
	if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0)
		fail("bind receiver");
	return fd;
}

static void fill_payload(struct payload *payload, unsigned int sequence)
{
	unsigned int index;

	payload->sequence = htonl(sequence);
	payload->inverse = htonl(~sequence);
	for (index = 0; index < sizeof(payload->bytes); index++)
		payload->bytes[index] = (unsigned char)(sequence * 29U + index * 17U);
}

static void send_exact(int tx, const struct sockaddr_in *remote,
		       const struct payload *payload)
{
	ssize_t length = sendto(tx, payload, sizeof(*payload), 0,
				(const struct sockaddr *)remote, sizeof(*remote));

	if (length != (ssize_t)sizeof(*payload))
		fail("sendto");
}

static void receive_exact(int rx, const struct payload *expected)
{
	struct payload actual;
	ssize_t length = recv(rx, &actual, sizeof(actual), 0);

	if (length != (ssize_t)sizeof(actual) ||
	    memcmp(&actual, expected, sizeof(actual))) {
		fprintf(stderr, "binary UDP payload changed\n");
		exit(EXIT_FAILURE);
	}
}

static void run_delivery(const char *ip, unsigned int send_port,
			 unsigned int receive_port, unsigned int count,
			 long delay_ns)
{
	struct sockaddr_in remote = address(ip, send_port);
	struct timespec delay = { .tv_nsec = delay_ns };
	struct payload payload;
	unsigned int sequence;
	int rx = receiver(ip, receive_port);
	int tx = socket(AF_INET, SOCK_DGRAM, 0);

	if (tx < 0)
		fail("socket sender");
	for (sequence = 0; sequence < count; sequence++) {
		fill_payload(&payload, sequence);
		send_exact(tx, &remote, &payload);
		receive_exact(rx, &payload);
		if (delay_ns && nanosleep(&delay, NULL) < 0 && errno != EINTR)
			fail("nanosleep");
	}
	close(tx);
	close(rx);
}

static void run_gso(unsigned int match_port, unsigned int replacement_port)
{
	unsigned char payload[24];
	unsigned char actual[8];
	unsigned char control[CMSG_SPACE(sizeof(uint16_t))];
	struct sockaddr_in remote = address("127.0.0.1", match_port);
	struct msghdr message;
	struct iovec vector;
	struct cmsghdr *item;
	uint16_t segment = sizeof(actual);
	ssize_t length;
	unsigned int index;
	int rx = receiver("127.0.0.1", replacement_port);
	int tx = socket(AF_INET, SOCK_DGRAM, 0);

	if (tx < 0)
		fail("socket GSO sender");
	for (index = 0; index < sizeof(payload); index++)
		payload[index] = (unsigned char)(0x40 + index);
	memset(&message, 0, sizeof(message));
	memset(control, 0, sizeof(control));
	vector.iov_base = payload;
	vector.iov_len = sizeof(payload);
	message.msg_name = &remote;
	message.msg_namelen = sizeof(remote);
	message.msg_iov = &vector;
	message.msg_iovlen = 1;
	message.msg_control = control;
	message.msg_controllen = sizeof(control);
	item = CMSG_FIRSTHDR(&message);
	item->cmsg_level = SOL_UDP;
	item->cmsg_type = UDP_SEGMENT;
	item->cmsg_len = CMSG_LEN(sizeof(segment));
	memcpy(CMSG_DATA(item), &segment, sizeof(segment));
	length = sendmsg(tx, &message, 0);
	if (length != (ssize_t)sizeof(payload))
		fail("sendmsg UDP_SEGMENT");
	for (index = 0; index < 3; index++) {
		length = recv(rx, actual, sizeof(actual), 0);
		if (length != (ssize_t)sizeof(actual) ||
		    memcmp(actual, payload + index * sizeof(actual),
			   sizeof(actual))) {
			fprintf(stderr, "GSO segment %u changed\n", index);
			exit(EXIT_FAILURE);
		}
	}
	close(tx);
	close(rx);
}

static void run_transition(unsigned int match_port,
			   unsigned int replacement_port)
{
	struct sockaddr_in remote = address("127.0.0.1", match_port);
	struct pollfd pollers[2];
	struct payload payload;
	unsigned int sequence;
	int tx = socket(AF_INET, SOCK_DGRAM, 0);

	if (tx < 0)
		fail("socket transition sender");
	pollers[0].fd = receiver("127.0.0.1", match_port);
	pollers[0].events = POLLIN;
	pollers[1].fd = receiver("127.0.0.1", replacement_port);
	pollers[1].events = POLLIN;
	for (sequence = 0; sequence < 1000; sequence++) {
		fill_payload(&payload, sequence);
		send_exact(tx, &remote, &payload);
		pollers[0].revents = 0;
		pollers[1].revents = 0;
		if (poll(pollers, 2, 3000) <= 0)
			fail("poll transition");
		if (pollers[0].revents & POLLIN)
			receive_exact(pollers[0].fd, &payload);
		else if (pollers[1].revents & POLLIN)
			receive_exact(pollers[1].fd, &payload);
		else
			fail("transition receive");
		usleep(2000);
	}
	close(pollers[0].fd);
	close(pollers[1].fd);
	close(tx);
}

int main(int argc, char **argv)
{
	unsigned int match_port;
	unsigned int replacement_port;
	unsigned int other_port;

	if (argc != 4) {
		fprintf(stderr, "usage: %s MODE MATCH_PORT REPLACEMENT_PORT\n",
			argv[0]);
		return EXIT_FAILURE;
	}
	match_port = parse_port(argv[2]);
	replacement_port = parse_port(argv[3]);
	other_port = replacement_port == 65535 ? replacement_port - 1 :
		       replacement_port + 1;
	if (!strcmp(argv[1], "redirect"))
		run_delivery("127.0.0.1", match_port, replacement_port, 1, 0);
	else if (!strcmp(argv[1], "burst"))
		run_delivery("127.0.0.1", match_port, replacement_port, 257,
			     1000000L);
	else if (!strcmp(argv[1], "direct"))
		run_delivery("127.0.0.1", other_port, other_port, 1, 0);
	else if (!strcmp(argv[1], "alternate"))
		run_delivery("127.0.0.2", match_port, match_port, 1, 0);
	else if (!strcmp(argv[1], "gso"))
		run_gso(match_port, replacement_port);
	else if (!strcmp(argv[1], "transition"))
		run_transition(match_port, replacement_port);
	else {
		fprintf(stderr, "unknown mode: %s\n", argv[1]);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
