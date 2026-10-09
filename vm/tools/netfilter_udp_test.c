// SPDX-License-Identifier: GPL-2.0
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define RK_MAGIC UINT32_C(0x524b5532)
#define RK_SENDERS 4U
#define RK_PAYLOAD_BYTES 64U

struct rk_payload {
	uint32_t magic;
	uint32_t sequence;
	uint32_t inverse;
	unsigned char bytes[RK_PAYLOAD_BYTES];
};

struct rk_shared {
	int sender;
	int receiver;
	struct sockaddr_in destination;
	unsigned int count;
	unsigned char *seen;
	atomic_int error;
};

struct rk_sender {
	struct rk_shared *shared;
	unsigned int first;
};

static void fill_payload(struct rk_payload *payload, unsigned int sequence)
{
	unsigned int index;

	payload->magic = htonl(RK_MAGIC);
	payload->sequence = htonl(sequence);
	payload->inverse = htonl(~sequence);
	for (index = 0; index < RK_PAYLOAD_BYTES; index++)
		payload->bytes[index] = (unsigned char)(sequence * 37U + index * 19U);
}

static int valid_payload(const struct rk_payload *payload, unsigned int count,
			 unsigned int *sequence)
{
	struct rk_payload expected;
	unsigned int decoded;

	if (ntohl(payload->magic) != RK_MAGIC)
		return 0;
	decoded = ntohl(payload->sequence);
	if (decoded >= count || ntohl(payload->inverse) != ~decoded)
		return 0;
	fill_payload(&expected, decoded);
	if (memcmp(payload, &expected, sizeof(expected)) != 0)
		return 0;
	*sequence = decoded;
	return 1;
}

static void *receive_all(void *argument)
{
	struct rk_shared *shared = argument;
	unsigned int received;

	for (received = 0; received < shared->count; received++) {
		struct rk_payload payload;
		unsigned int sequence;
		ssize_t length;

		length = recv(shared->receiver, &payload, sizeof(payload), 0);
		if (length != (ssize_t)sizeof(payload) ||
		    !valid_payload(&payload, shared->count, &sequence) ||
		    shared->seen[sequence] != 0) {
			atomic_store(&shared->error, 1);
			return NULL;
		}
		shared->seen[sequence] = 1;
	}
	return NULL;
}

static void *send_partition(void *argument)
{
	struct rk_sender *sender = argument;
	struct rk_shared *shared = sender->shared;
	struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000L};
	unsigned int sequence;

	for (sequence = sender->first; sequence < shared->count;
	     sequence += RK_SENDERS) {
		struct rk_payload payload;
		ssize_t length;

		fill_payload(&payload, sequence);
		length = sendto(shared->sender, &payload, sizeof(payload), 0,
				(const struct sockaddr *)&shared->destination,
				sizeof(shared->destination));
		if (length != (ssize_t)sizeof(payload)) {
			atomic_store(&shared->error, 1);
			return NULL;
		}
		(void)nanosleep(&pause, NULL);
	}
	return NULL;
}

static int parse_count(const char *text, unsigned int *count)
{
	char *end;
	unsigned long value;

	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || value == 0 ||
	    value > 100000)
		return -1;
	*count = (unsigned int)value;
	return 0;
}

int main(int argc, char **argv)
{
	struct timeval timeout = {.tv_sec = 3, .tv_usec = 0};
	struct sockaddr_in bound = {.sin_family = AF_INET};
	struct rk_shared shared = {.sender = -1, .receiver = -1};
	struct rk_sender senders[RK_SENDERS];
	pthread_t sender_threads[RK_SENDERS];
	pthread_t receiver_thread;
	socklen_t bound_length = sizeof(bound);
	unsigned int index;
	int result = 1;

	if (argc != 2 || parse_count(argv[1], &shared.count) != 0) {
		fprintf(stderr, "usage: %s COUNT\n", argv[0]);
		return 2;
	}
	shared.seen = calloc(shared.count, sizeof(*shared.seen));
	if (!shared.seen) {
		perror("calloc");
		return 2;
	}

	shared.receiver = socket(AF_INET, SOCK_DGRAM, 0);
	shared.sender = socket(AF_INET, SOCK_DGRAM, 0);
	if (shared.receiver < 0 || shared.sender < 0) {
		perror("socket");
		goto out;
	}
	bound.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(shared.receiver, (const struct sockaddr *)&bound,
		 sizeof(bound)) != 0 ||
	    getsockname(shared.receiver, (struct sockaddr *)&bound,
			&bound_length) != 0 ||
	    setsockopt(shared.receiver, SOL_SOCKET, SO_RCVTIMEO, &timeout,
		       sizeof(timeout)) != 0) {
		perror("receiver setup");
		goto out;
	}
	shared.destination = bound;

	if (pthread_create(&receiver_thread, NULL, receive_all, &shared) != 0) {
		fprintf(stderr, "could not create receiver thread\n");
		goto out;
	}
	for (index = 0; index < RK_SENDERS; index++) {
		senders[index].shared = &shared;
		senders[index].first = index;
		if (pthread_create(&sender_threads[index], NULL, send_partition,
				   &senders[index]) != 0) {
			fprintf(stderr, "could not create sender thread %u\n", index);
			atomic_store(&shared.error, 1);
			break;
		}
	}
	while (index > 0) {
		index--;
		(void)pthread_join(sender_threads[index], NULL);
	}
	(void)pthread_join(receiver_thread, NULL);
	if (atomic_load(&shared.error) != 0) {
		fprintf(stderr, "UDP payload loss, duplication, or corruption\n");
		goto out;
	}
	for (index = 0; index < shared.count; index++) {
		if (shared.seen[index] == 0) {
			fprintf(stderr, "missing sequence %u\n", index);
			goto out;
		}
	}
	printf("verified %u unique UDP payloads\n", shared.count);
	result = 0;

out:
	if (shared.sender >= 0)
		(void)close(shared.sender);
	if (shared.receiver >= 0)
		(void)close(shared.receiver);
	free(shared.seen);
	return result;
}
