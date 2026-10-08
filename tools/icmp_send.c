// Send one ICMP echo request with a custom echo ID and optional payload.
// Usage: icmp_send <ip> <echo_id_hex> [payload]
//        icmp_send <ip> <echo_id_hex> --hex <payload_hex>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct icmphdr {
	unsigned char type;
	unsigned char code;
	unsigned short checksum;
	unsigned short id;
	unsigned short seq;
};

static unsigned short cksum(void *data, int len)
{
	unsigned int sum = 0;
	unsigned short *p = data;
	while (len > 1) {
		sum += *p++;
		len -= 2;
	}
	if (len)
		sum += *(unsigned char *)p;
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return ~sum;
}

int main(int argc, char **argv)
{
	unsigned char *payload;
	size_t plen;
	size_t i;

	if (argc < 3) {
		fprintf(stderr,
			"usage: %s <ip> <id_hex> [payload]\n"
			"       %s <ip> <id_hex> --hex <payload_hex>\n",
			argv[0], argv[0]);
		return 1;
	}

	unsigned short id = strtol(argv[2], NULL, 16);
	if (argc == 5 && strcmp(argv[3], "--hex") == 0) {
		const char *hex = argv[4];

		if (strlen(hex) % 2 != 0)
			return 1;
		plen = strlen(hex) / 2;
		payload = malloc(plen ? plen : 1);
		if (!payload)
			return 1;
		for (i = 0; i < plen; i++) {
			unsigned int byte;

			if (sscanf(hex + i * 2, "%2x", &byte) != 1) {
				free(payload);
				return 1;
			}
			payload[i] = byte;
		}
	} else {
		const char *text = argc > 3 ? argv[3] : "";

		plen = strlen(text);
		payload = malloc(plen ? plen : 1);
		if (!payload)
			return 1;
		memcpy(payload, text, plen);
	}

	int total = sizeof(struct icmphdr) + (int)plen;
	char *buf = calloc(1, total);
	if (!buf) {
		free(payload);
		return 1;
	}
	struct icmphdr *h = (struct icmphdr *)buf;
	h->type = 8;
	h->id = htons(id);
	h->seq = htons(1);
	memcpy(buf + sizeof(*h), payload, plen);
	h->checksum = cksum(buf, total);

	int fd = socket(AF_INET, SOCK_RAW, 1);
	if (fd < 0) {
		perror("socket");
		free(payload);
		free(buf);
		return 1;
	}

	struct sockaddr_in dst = {.sin_family = AF_INET};
	inet_pton(AF_INET, argv[1], &dst.sin_addr);
	sendto(fd, buf, total, 0, (void *)&dst, sizeof(dst));

	close(fd);
	free(payload);
	free(buf);
	return 0;
}
