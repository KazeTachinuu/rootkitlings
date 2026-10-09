// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <linux/udp.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef SOL_UDP
#define SOL_UDP 17
#endif

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
	if (errno || *text == '\0' || *end != '\0' || !value || value > 65535) {
		fprintf(stderr, "invalid port: %s\n", text);
		exit(EXIT_FAILURE);
	}
	return (unsigned int)value;
}

static struct sockaddr_in loopback(unsigned int port)
{
	struct sockaddr_in address;

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons((unsigned short)port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	return address;
}

static int receiver(unsigned int port)
{
	struct sockaddr_in address = loopback(port);
	struct timeval timeout = { .tv_sec = 2 };
	int fd;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		fail("socket receiver");
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
		       sizeof(timeout)) < 0)
		fail("setsockopt timeout");
	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
		fail("bind receiver");
	return fd;
}

static void udp_delivery(unsigned int port)
{
	static const unsigned char payload[] = "bounded-udp";
	unsigned char received[sizeof(payload)];
	struct sockaddr_in address = loopback(port);
	ssize_t length;
	int rx = receiver(port);
	int tx = socket(AF_INET, SOCK_DGRAM, 0);

	if (tx < 0)
		fail("socket sender");
	length = sendto(tx, payload, sizeof(payload), 0,
			(struct sockaddr *)&address, sizeof(address));
	if (length != (ssize_t)sizeof(payload))
		fail("sendto UDP");
	length = recv(rx, received, sizeof(received), 0);
	if (length != (ssize_t)sizeof(payload) ||
	    memcmp(received, payload, sizeof(payload))) {
		fprintf(stderr, "UDP payload changed\n");
		exit(EXIT_FAILURE);
	}
	close(tx);
	close(rx);
}

struct burst_payload {
	uint32_t sequence;
	uint32_t inverse;
	unsigned char bytes[32];
};

static void fill_burst_payload(struct burst_payload *payload,
			       unsigned int sequence)
{
	unsigned int index;

	payload->sequence = htonl(sequence);
	payload->inverse = htonl(~sequence);
	for (index = 0; index < sizeof(payload->bytes); index++)
		payload->bytes[index] = (unsigned char)(sequence * 29U + index * 17U);
}

static void udp_burst(unsigned int port, unsigned int count, long delay_ns)
{
	struct sockaddr_in address = loopback(port);
	struct timespec delay = { .tv_nsec = delay_ns };
	struct burst_payload expected;
	struct burst_payload received;
	unsigned int sequence;
	ssize_t length;
	int rx = receiver(port);
	int tx = socket(AF_INET, SOCK_DGRAM, 0);

	if (tx < 0)
		fail("socket burst sender");
	for (sequence = 0; sequence < count; sequence++) {
		fill_burst_payload(&expected, sequence);
		length = sendto(tx, &expected, sizeof(expected), 0,
				(struct sockaddr *)&address, sizeof(address));
		if (length != (ssize_t)sizeof(expected))
			fail("sendto burst");
		length = recv(rx, &received, sizeof(received), 0);
		if (length != (ssize_t)sizeof(received) ||
		    memcmp(&received, &expected, sizeof(received))) {
			fprintf(stderr, "burst payload %u changed\n", sequence);
			exit(EXIT_FAILURE);
		}
		if (nanosleep(&delay, NULL) < 0 && errno != EINTR)
			fail("nanosleep burst");
	}
	close(tx);
	close(rx);
}

static void udp_gso(unsigned int port)
{
	unsigned char payload[24];
	unsigned char received[8];
	unsigned char control[CMSG_SPACE(sizeof(uint16_t))];
	struct sockaddr_in address = loopback(port);
	struct msghdr message;
	struct iovec vector;
	struct cmsghdr *control_message;
	uint16_t segment = sizeof(received);
	ssize_t length;
	int index;
	int rx = receiver(port);
	int tx = socket(AF_INET, SOCK_DGRAM, 0);

	if (tx < 0)
		fail("socket GSO sender");
	for (index = 0; index < (int)sizeof(payload); index++)
		payload[index] = (unsigned char)(0x40 + index);
	memset(&message, 0, sizeof(message));
	memset(control, 0, sizeof(control));
	vector.iov_base = payload;
	vector.iov_len = sizeof(payload);
	message.msg_name = &address;
	message.msg_namelen = sizeof(address);
	message.msg_iov = &vector;
	message.msg_iovlen = 1;
	message.msg_control = control;
	message.msg_controllen = sizeof(control);
	control_message = CMSG_FIRSTHDR(&message);
	control_message->cmsg_level = SOL_UDP;
	control_message->cmsg_type = UDP_SEGMENT;
	control_message->cmsg_len = CMSG_LEN(sizeof(segment));
	memcpy(CMSG_DATA(control_message), &segment, sizeof(segment));
	length = sendmsg(tx, &message, 0);
	if (length != (ssize_t)sizeof(payload))
		fail("sendmsg UDP_SEGMENT");
	for (index = 0; index < 3; index++) {
		length = recv(rx, received, sizeof(received), 0);
		if (length != (ssize_t)sizeof(received) ||
		    memcmp(received, payload + index * sizeof(received),
			   sizeof(received))) {
			fprintf(stderr, "GSO segment %d changed\n", index);
			exit(EXIT_FAILURE);
		}
	}
	close(tx);
	close(rx);
}

static void put_be16(unsigned char *bytes, unsigned int value)
{
	bytes[0] = value >> 8;
	bytes[1] = value;
}

static void raw_packet(int fd, unsigned int port, unsigned int ihl,
		       unsigned int fragment, unsigned int identifier,
		       unsigned int udp_length, unsigned int checksum,
		       unsigned int extra)
{
	unsigned char packet[80];
	struct sockaddr_in address = loopback(port);
	unsigned int header_length = ihl * 4;
	unsigned int length = header_length + 8 + extra;
	unsigned int index;
	ssize_t sent;

	memset(packet, 0, sizeof(packet));
	packet[0] = 0x40 | ihl;
	put_be16(packet + 4, identifier);
	put_be16(packet + 6, fragment);
	packet[8] = 64;
	packet[9] = 17;
	packet[12] = 127;
	packet[15] = 1;
	packet[16] = 127;
	packet[19] = 1;
	for (index = 20; index < header_length; index++)
		packet[index] = 1;
	put_be16(packet + header_length, 0x1234);
	put_be16(packet + header_length + 2, port);
	put_be16(packet + header_length + 4, udp_length);
	put_be16(packet + header_length + 6, checksum);
	for (index = 0; index < extra; index++)
		packet[header_length + 8 + index] = (unsigned char)(0xa0 + index);
	sent = sendto(fd, packet, length, 0, (struct sockaddr *)&address,
		      sizeof(address));
	if (sent != (ssize_t)length)
		fail("sendto raw packet");
}

static int raw_socket(void)
{
	int enabled = 1;
	int fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);

	if (fd < 0)
		fail("socket raw");
	if (setsockopt(fd, IPPROTO_IP, IP_HDRINCL, &enabled,
		       sizeof(enabled)) < 0)
		fail("setsockopt IP_HDRINCL");
	return fd;
}

static void raw_case(const char *mode, unsigned int port)
{
	unsigned char byte;
	ssize_t length;
	int rx = -1;
	int fd = raw_socket();

	if (!strcmp(mode, "options")) {
		rx = receiver(port);
		raw_packet(fd, port, 6, 0, 0x4101, 8, 0, 0);
		length = recv(rx, &byte, sizeof(byte), 0);
		if (length != 0) {
			fprintf(stderr, "option packet was not delivered as empty UDP\n");
			exit(EXIT_FAILURE);
		}
	} else if (!strcmp(mode, "malformed-short")) {
		raw_packet(fd, port, 5, 0, 0x4102, 7, 0, 0);
	} else if (!strcmp(mode, "malformed-long")) {
		raw_packet(fd, port, 5, 0, 0x4103, 40, 0, 0);
	} else if (!strcmp(mode, "bad-checksum")) {
		raw_packet(fd, port, 5, 0, 0x4104, 8, 0x1234, 0);
	} else if (!strcmp(mode, "surplus")) {
		raw_packet(fd, port, 5, 0, 0x4105, 8, 0, 4);
	} else if (!strcmp(mode, "fragments")) {
		raw_packet(fd, port, 5, 0x2000, 0x4106, 24, 0, 8);
		raw_packet(fd, port, 5, 0x0002, 0x4106, 0, 0, 0);
	} else {
		fprintf(stderr, "unknown raw mode: %s\n", mode);
		exit(EXIT_FAILURE);
	}
	if (rx >= 0)
		close(rx);
	close(fd);
}

int main(int argc, char **argv)
{
	unsigned int port;

	if (argc != 3) {
		fprintf(stderr, "usage: %s MODE PORT\n", argv[0]);
		return EXIT_FAILURE;
	}
	port = parse_port(argv[2]);
	if (!strcmp(argv[1], "ordinary"))
		udp_delivery(port);
	else if (!strcmp(argv[1], "wrong-port"))
		udp_delivery(port == 65535 ? port - 1 : port + 1);
	else if (!strcmp(argv[1], "burst"))
		udp_burst(port, 257, 1000000L);
	else if (!strcmp(argv[1], "long-burst"))
		udp_burst(port, 1000, 2000000L);
	else if (!strcmp(argv[1], "gso"))
		udp_gso(port);
	else
		raw_case(argv[1], port);
	return EXIT_SUCCESS;
}
