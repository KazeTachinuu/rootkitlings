#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

enum {
	KEY_LENGTH = 32,
	NONCE_LENGTH = 12,
	TAG_LENGTH = 16,
	HEADER_LENGTH = 18,
	FRAME_MAX = 128,
	SHELL_TRANSCRIPT_MAX = 8192,
	REQUEST = 1,
	REPLY = 2,
};

static uint8_t key[KEY_LENGTH];

static const uint8_t sbox[256] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
	0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
	0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
	0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

struct channel {
	uint64_t session;
	uint64_t request_sequence;
	uint64_t reply_sequence;
};

static void fail(const char *operation)
{
	perror(operation);
	exit(EXIT_FAILURE);
}

static int hex_value(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

static int parse_key(const char *text)
{
	uint8_t candidate[KEY_LENGTH];
	uint8_t combined = 0;
	size_t index;

	if (!text || strlen(text) != KEY_LENGTH * 2)
		return -1;
	for (index = 0; index < KEY_LENGTH; index++) {
		int high = hex_value(text[index * 2]);
		int low = hex_value(text[index * 2 + 1]);

		if (high < 0 || low < 0) {
			memset(candidate, 0, sizeof(candidate));
			return -1;
		}
		candidate[index] = (uint8_t)((high << 4) | low);
		combined |= candidate[index];
	}
	if (!combined) {
		memset(candidate, 0, sizeof(candidate));
		return -1;
	}
	memcpy(key, candidate, sizeof(key));
	memset(candidate, 0, sizeof(candidate));
	return 0;
}

static uint8_t xtime(uint8_t value)
{
	return (uint8_t)((value << 1) ^ ((value >> 7) * 0x1b));
}

static void key_expand(const uint8_t input[32], uint8_t expanded[240])
{
	uint8_t rcon = 1;
	size_t generated = 32;

	memcpy(expanded, input, 32);
	while (generated < 240) {
		uint8_t word[4];
		size_t index;

		memcpy(word, expanded + generated - 4, sizeof(word));
		if (generated % 32 == 0) {
			uint8_t first = word[0];

			word[0] = sbox[word[1]];
			word[1] = sbox[word[2]];
			word[2] = sbox[word[3]];
			word[3] = sbox[first];
			word[0] ^= rcon;
			rcon = xtime(rcon);
		} else if (generated % 32 == 16) {
			for (index = 0; index < 4; index++)
				word[index] = sbox[word[index]];
		}
		for (index = 0; index < 4; index++) {
			expanded[generated] =
				expanded[generated - 32] ^ word[index];
			generated++;
		}
	}
}

static void shift_rows(uint8_t state[16])
{
	uint8_t temporary;

	temporary = state[1];
	state[1] = state[5];
	state[5] = state[9];
	state[9] = state[13];
	state[13] = temporary;
	temporary = state[2];
	state[2] = state[10];
	state[10] = temporary;
	temporary = state[6];
	state[6] = state[14];
	state[14] = temporary;
	temporary = state[3];
	state[3] = state[15];
	state[15] = state[11];
	state[11] = state[7];
	state[7] = temporary;
}

static void mix_columns(uint8_t state[16])
{
	size_t column;

	for (column = 0; column < 4; column++) {
		uint8_t *value = state + column * 4;
		uint8_t all = value[0] ^ value[1] ^ value[2] ^ value[3];
		uint8_t first = value[0];

		value[0] ^= all ^ xtime(value[0] ^ value[1]);
		value[1] ^= all ^ xtime(value[1] ^ value[2]);
		value[2] ^= all ^ xtime(value[2] ^ value[3]);
		value[3] ^= all ^ xtime(value[3] ^ first);
	}
}

static void aes_encrypt(const uint8_t input[16], uint8_t output[16])
{
	uint8_t expanded[240];
	uint8_t state[16];
	size_t index;
	unsigned int round;

	key_expand(key, expanded);
	for (index = 0; index < 16; index++)
		state[index] = input[index] ^ expanded[index];
	for (round = 1; round < 14; round++) {
		for (index = 0; index < 16; index++)
			state[index] = sbox[state[index]];
		shift_rows(state);
		mix_columns(state);
		for (index = 0; index < 16; index++)
			state[index] ^= expanded[round * 16 + index];
	}
	for (index = 0; index < 16; index++)
		state[index] = sbox[state[index]];
	shift_rows(state);
	for (index = 0; index < 16; index++)
		output[index] = state[index] ^ expanded[224 + index];
	memset(expanded, 0, sizeof(expanded));
	memset(state, 0, sizeof(state));
}

static void block_xor(uint8_t target[16], const uint8_t source[16])
{
	size_t index;

	for (index = 0; index < 16; index++)
		target[index] ^= source[index];
}

static void gf_multiply(uint8_t value[16], const uint8_t factor[16])
{
	uint8_t product[16] = { 0 };
	uint8_t current[16];
	size_t bit;

	memcpy(current, factor, sizeof(current));
	for (bit = 0; bit < 128; bit++) {
		unsigned int selected =
			(value[bit / 8] >> (7 - bit % 8)) & 1U;
		unsigned int carry = current[15] & 1U;
		int index;

		if (selected)
			block_xor(product, current);
		for (index = 15; index > 0; index--)
			current[index] = (uint8_t)(
				(current[index] >> 1) |
				((current[index - 1] & 1U) << 7));
		current[0] >>= 1;
		if (carry)
			current[0] ^= 0xe1;
	}
	memcpy(value, product, sizeof(product));
}

static void ghash_bytes(uint8_t hash[16], const uint8_t h[16],
			const uint8_t *bytes, size_t length)
{
	while (length) {
		uint8_t block[16] = { 0 };
		size_t count = length < sizeof(block) ? length : sizeof(block);

		memcpy(block, bytes, count);
		block_xor(hash, block);
		gf_multiply(hash, h);
		bytes += count;
		length -= count;
	}
}

static void write_be64(uint8_t output[8], uint64_t value)
{
	int index;

	for (index = 7; index >= 0; index--) {
		output[index] = (uint8_t)value;
		value >>= 8;
	}
}

static uint64_t read_be64(const uint8_t input[8])
{
	uint64_t value = 0;
	size_t index;

	for (index = 0; index < 8; index++)
		value = (value << 8) | input[index];
	return value;
}

static void increment_counter(uint8_t counter[16])
{
	int index;

	for (index = 15; index >= 12; index--)
		if (++counter[index])
			break;
}

static void make_nonce(uint64_t session, uint64_t sequence, uint8_t kind,
		       uint8_t nonce[12])
{
	uint32_t counter = (uint32_t)(sequence << 1) | (kind == REPLY);

	write_be64(nonce, session);
	nonce[8] = (uint8_t)(counter >> 24);
	nonce[9] = (uint8_t)(counter >> 16);
	nonce[10] = (uint8_t)(counter >> 8);
	nonce[11] = (uint8_t)counter;
}

static void gcm_crypt(const uint8_t nonce[12], const uint8_t *aad,
		      size_t aad_length, const uint8_t *input,
		      size_t input_length, uint8_t *output, uint8_t tag[16],
		      int decrypt)
{
	uint8_t h[16] = { 0 };
	uint8_t j0[16] = { 0 };
	uint8_t counter[16];
	uint8_t hash[16] = { 0 };
	uint8_t lengths[16] = { 0 };
	uint8_t mask[16];
	size_t offset = 0;

	aes_encrypt(h, h);
	memcpy(j0, nonce, 12);
	j0[15] = 1;
	memcpy(counter, j0, sizeof(counter));
	while (offset < input_length) {
		uint8_t stream[16];
		size_t count = input_length - offset;
		size_t index;

		if (count > sizeof(stream))
			count = sizeof(stream);
		increment_counter(counter);
		aes_encrypt(counter, stream);
		for (index = 0; index < count; index++)
			output[offset + index] = input[offset + index] ^ stream[index];
		offset += count;
	}
	ghash_bytes(hash, h, aad, aad_length);
	ghash_bytes(hash, h, decrypt ? input : output, input_length);
	write_be64(lengths, (uint64_t)aad_length * 8);
	write_be64(lengths + 8, (uint64_t)input_length * 8);
	block_xor(hash, lengths);
	gf_multiply(hash, h);
	aes_encrypt(j0, mask);
	block_xor(hash, mask);
	memcpy(tag, hash, TAG_LENGTH);
}

static int tag_equal(const uint8_t left[16], const uint8_t right[16])
{
	unsigned int difference = 0;
	size_t index;

	for (index = 0; index < 16; index++)
		difference |= left[index] ^ right[index];
	return difference == 0;
}

static size_t seal_envelope(uint8_t kind, uint64_t session,
			    uint64_t sequence, const char *plaintext,
			    uint8_t envelope[FRAME_MAX])
{
	uint8_t nonce[NONCE_LENGTH];
	uint8_t tag[TAG_LENGTH];
	size_t length = strlen(plaintext);

	if (length > FRAME_MAX - HEADER_LENGTH - TAG_LENGTH)
		fail("plaintext too long");
	envelope[0] = 1;
	envelope[1] = kind;
	write_be64(envelope + 2, session);
	write_be64(envelope + 10, sequence);
	make_nonce(session, sequence, kind, nonce);
	gcm_crypt(nonce, envelope, HEADER_LENGTH,
		  (const uint8_t *)plaintext, length,
		  envelope + HEADER_LENGTH, tag, 0);
	memcpy(envelope + HEADER_LENGTH + length, tag, sizeof(tag));
	return HEADER_LENGTH + length + TAG_LENGTH;
}

static int open_envelope(const uint8_t *envelope, size_t length,
			 uint8_t kind, uint64_t session, uint64_t sequence,
			 char plaintext[FRAME_MAX])
{
	uint8_t nonce[NONCE_LENGTH];
	uint8_t tag[TAG_LENGTH];
	size_t cipher_length;

	if (length < HEADER_LENGTH + TAG_LENGTH || length > FRAME_MAX ||
	    envelope[0] != 1 || envelope[1] != kind ||
	    read_be64(envelope + 2) != session ||
	    read_be64(envelope + 10) != sequence)
		return -1;
	cipher_length = length - HEADER_LENGTH - TAG_LENGTH;
	make_nonce(session, sequence, kind, nonce);
	gcm_crypt(nonce, envelope, HEADER_LENGTH,
		  envelope + HEADER_LENGTH, cipher_length,
		  (uint8_t *)plaintext, tag, 1);
	if (!tag_equal(tag, envelope + HEADER_LENGTH + cipher_length)) {
		memset(plaintext, 0, cipher_length);
		return -1;
	}
	plaintext[cipher_length] = '\0';
	return 0;
}

static void write_all(int fd, const void *buffer, size_t length)
{
	const uint8_t *bytes = buffer;

	while (length) {
		ssize_t count = write(fd, bytes, length);

		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			fail("write");
		bytes += count;
		length -= (size_t)count;
	}
}

static void read_all(int fd, void *buffer, size_t length)
{
	uint8_t *bytes = buffer;

	while (length) {
		ssize_t count = read(fd, bytes, length);

		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0) {
			if (!count)
				errno = EPIPE;
			fail("read");
		}
		bytes += count;
		length -= (size_t)count;
	}
}

static void send_frame(int fd, const uint8_t *bytes, size_t length)
{
	uint8_t header[2] = {
		(uint8_t)(length >> 8),
		(uint8_t)length,
	};

	write_all(fd, header, sizeof(header));
	write_all(fd, bytes, length);
}

static size_t receive_frame(int fd, uint8_t bytes[FRAME_MAX])
{
	uint8_t header[2];
	size_t length;

	read_all(fd, header, sizeof(header));
	length = ((size_t)header[0] << 8) | header[1];
	if (!length || length > FRAME_MAX) {
		errno = EPROTO;
		fail("frame length");
	}
	read_all(fd, bytes, length);
	return length;
}

static void expect_eof(int fd)
{
	uint8_t byte;
	ssize_t count;

	do {
		count = read(fd, &byte, 1);
	} while (count < 0 && errno == EINTR);
	if (count != 0) {
		errno = EPROTO;
		fail("expected EOF");
	}
}

static size_t read_shell_transcript(int fd, char *buffer, size_t capacity)
{
	int input_closed = 0;
	size_t length = 0;

	while (length + 1 < capacity) {
		struct pollfd ready = { .fd = fd, .events = POLLIN };
		ssize_t count;

		if (poll(&ready, 1, 5000) <= 0) {
			buffer[length] = '\0';
			fputs("--- stalled root shell transcript ---\n", stderr);
			(void)fwrite(buffer, 1, length, stderr);
			fputs("\n--- end transcript ---\n", stderr);
			errno = ETIMEDOUT;
			fail("read root PTY Bash");
		}
		count = read(fd, buffer + length, capacity - length - 1);
		if (count < 0 && errno == EINTR)
			continue;
		if (count < 0)
			fail("read shell transcript");
		if (!count) {
			buffer[length] = '\0';
			return length;
		}
		length += (size_t)count;
		buffer[length] = '\0';
		if (!input_closed && strstr(buffer, "rkproof-uid=0") &&
		    strstr(buffer, "rkproof-tty=yes") &&
		    strstr(buffer, "rkproof-bash=") &&
		    strstr(buffer, "rkproof-command")) {
			if (shutdown(fd, SHUT_WR) < 0)
				fail("shutdown completed shell input");
			input_closed = 1;
		}
	}
	errno = EOVERFLOW;
	fail("shell transcript limit");
	return 0;
}

static void drain_shell_eof(int fd)
{
	char buffer[1024];
	size_t total = 0;

	for (;;) {
		struct pollfd ready = { .fd = fd, .events = POLLIN };
		ssize_t count;

		if (poll(&ready, 1, 5000) <= 0) {
			errno = ETIMEDOUT;
			fail("wait for shell EOF");
		}
		count = read(fd, buffer, sizeof(buffer));
		if (count < 0 && errno == EINTR)
			continue;
		if (count < 0)
			fail("drain shell output");
		if (!count)
			return;
		total += (size_t)count;
		if (total > SHELL_TRANSCRIPT_MAX) {
			errno = EOVERFLOW;
			fail("shell drain limit");
		}
	}
}

static void expect_no_peer(int server)
{
	struct pollfd descriptor = { .fd = server, .events = POLLIN };
	int ready;

	do {
		ready = poll(&descriptor, 1, 200);
	} while (ready < 0 && errno == EINTR);
	if (ready < 0)
		fail("poll shell listener");
	if (ready) {
		errno = EPROTO;
		fail("unexpected shell connection");
	}
}

static int listener(const char *address_text, unsigned short requested,
		    unsigned short *actual, int listen_now)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(requested),
	};
	socklen_t length = sizeof(address);
	int reuse = 1;
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);

	if (inet_pton(AF_INET, address_text, &address.sin_addr) != 1) {
		errno = EINVAL;
		fail("listener address");
	}
	if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
				 sizeof(reuse)) < 0 ||
	    bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
	    getsockname(fd, (struct sockaddr *)&address, &length) < 0 ||
	    (listen_now && listen(fd, 4) < 0))
		fail("listener");
	*actual = ntohs(address.sin_port);
	return fd;
}

static int accept_peer(int server)
{
	struct timeval timeout = { .tv_sec = 10 };
	int peer = accept4(server, NULL, NULL, SOCK_CLOEXEC);

	if (peer < 0 || setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout,
				   sizeof(timeout)) < 0)
		fail("accept");
	return peer;
}

static void marker(const char *directory, const char *name)
{
	char path[512];
	FILE *stream;

	if (snprintf(path, sizeof(path), "%s/%s", directory, name) >=
	    (int)sizeof(path)) {
		errno = ENAMETOOLONG;
		fail("marker path");
	}
	stream = fopen(path, "w");
	if (!stream || fputs("1\n", stream) == EOF || fclose(stream))
		fail("marker");
}

static uint64_t receive_hello(int peer, uint64_t previous,
			      struct channel *channel)
{
	uint8_t envelope[FRAME_MAX];
	char plaintext[FRAME_MAX];
	size_t length = receive_frame(peer, envelope);
	uint64_t session;

	if (length < HEADER_LENGTH || envelope[0] != 1 ||
	    envelope[1] != REPLY) {
		errno = EPROTO;
		fail("HELLO header");
	}
	session = read_be64(envelope + 2);
	if (!session || session == previous ||
	    open_envelope(envelope, length, REPLY, session, 1, plaintext) ||
	    strcmp(plaintext, "HELLO")) {
		errno = EPROTO;
		fail("authenticated HELLO");
	}
	channel->session = session;
	channel->request_sequence = 1;
	channel->reply_sequence = 2;
	return session;
}

static void command(int peer, struct channel *channel, const char *request,
		    char reply[FRAME_MAX])
{
	uint8_t envelope[FRAME_MAX];
	size_t length;

	length = seal_envelope(REQUEST, channel->session,
			       channel->request_sequence++, request, envelope);
	send_frame(peer, envelope, length);
	length = receive_frame(peer, envelope);
	if (open_envelope(envelope, length, REPLY, channel->session,
			  channel->reply_sequence++, reply)) {
		errno = EPROTO;
		fail("authenticated reply");
	}
}

static void expect_reply(int peer, struct channel *channel,
			 const char *request, const char *expected)
{
	char reply[FRAME_MAX];

	command(peer, channel, request, reply);
	if (strcmp(reply, expected)) {
		errno = EPROTO;
		fail("reply text");
	}
}

static void parse_ticket(const char *reply, uint32_t *request_id,
			 uint64_t *cookie)
{
	unsigned int parsed_id;
	unsigned long long parsed_cookie;
	char tail;

	if (sscanf(reply, "OPEN %8x %16llx%c", &parsed_id,
		   &parsed_cookie, &tail) != 2 || !parsed_id || !parsed_cookie) {
		errno = EPROTO;
		fail("shell ticket");
	}
	*request_id = parsed_id;
	*cookie = parsed_cookie;
}

static int accept_shell(int server, uint32_t request_id, uint64_t cookie)
{
	char expected[32];
	char prelude[31];
	int peer = accept_peer(server);

	if (snprintf(expected, sizeof(expected), "RKSH %08x %016llx\n",
		     request_id, (unsigned long long)cookie) !=
	    (int)sizeof(prelude)) {
		errno = EPROTO;
		fail("shell prelude format");
	}
	read_all(peer, prelude, sizeof(prelude));
	if (memcmp(prelude, expected, sizeof(prelude))) {
		errno = EACCES;
		fail("shell ownership cookie");
	}
	return peer;
}

static void run_shell(int peer)
{
	static const char commands[] =
		"printf 'rkproof-%s=%s\\n' uid \"$(id -u)\"\n"
		"if test -t 0 && test -t 1 && test -t 2; then "
		"value=yes; else value=no; fi; "
		"printf 'rkproof-%s=%s\\n' tty \"$value\"\n"
		"printf 'rkproof-%s=%s\\n' bash "
		"\"${BASH_VERSION%%.*}\"\n"
		"printf 'rkproof-%s\\n' command\n"
		"exit\n";
	char output[SHELL_TRANSCRIPT_MAX];
	const char *bash;
	size_t length;

	write_all(peer, commands, sizeof(commands) - 1);
	length = read_shell_transcript(peer, output, sizeof(output));
	bash = strstr(output, "rkproof-bash=");
	if (!strstr(output, "rkproof-uid=0") ||
	    !strstr(output, "rkproof-tty=yes") ||
	    !strstr(output, "rkproof-command") || !bash ||
	    bash[sizeof("rkproof-bash=") - 1] < '1' ||
	    bash[sizeof("rkproof-bash=") - 1] > '9') {
		fputs("--- root shell transcript ---\n", stderr);
		(void)fwrite(output, 1, length, stderr);
		fputs("\n--- end transcript ---\n", stderr);
		errno = EPROTO;
		fail("root PTY Bash proof");
	}
	close(peer);
}

static void wait_shell_state(int peer, struct channel *channel,
			     uint32_t request_id, const char *state,
			     int result)
{
	char expected[FRAME_MAX];
	char reply[FRAME_MAX];
	unsigned int attempt;

	if (snprintf(expected, sizeof(expected), "STATE %08x %s %d",
		     request_id, state, result) >= (int)sizeof(expected))
		fail("state text");
	for (attempt = 0; attempt < 500; attempt++) {
		command(peer, channel, "SHELL STATUS", reply);
		if (!strcmp(reply, expected))
			return;
		usleep(10000);
	}
	errno = ETIMEDOUT;
	fail("shell state");
}

static int reserve(const char *path)
{
	unsigned short port;
	FILE *stream;
	int fd = listener("127.0.0.1", 0, &port, 0);

	close(fd);
	stream = fopen(path, "w");
	if (!stream || fprintf(stream, "%u\n", port) < 0 || fclose(stream))
		fail("reserve");
	return 0;
}

static int crypto_selftest(void)
{
	static const uint8_t zero_key[32] = { 0 };
	static const uint8_t nonce[12] = { 0 };
	static const uint8_t plaintext[16] = { 0 };
	static const uint8_t ciphertext[16] = {
		0xce, 0xa7, 0x40, 0x3d, 0x4d, 0x60, 0x6b, 0x6e,
		0x07, 0x4e, 0xc5, 0xd3, 0xba, 0xf3, 0x9d, 0x18,
	};
	static const uint8_t expected_tag[16] = {
		0xd0, 0xd1, 0xc8, 0xa7, 0x99, 0x99, 0x6b, 0xf0,
		0x26, 0x5b, 0x98, 0xb5, 0xd4, 0x8a, 0xb9, 0x19,
	};
	uint8_t saved[32];
	uint8_t output[16];
	uint8_t tag[16];

	memcpy(saved, key, sizeof(saved));
	memcpy(key, zero_key, sizeof(zero_key));
	gcm_crypt(nonce, NULL, 0, plaintext, sizeof(plaintext), output, tag, 0);
	memcpy(key, saved, sizeof(saved));
	if (memcmp(output, ciphertext, sizeof(output)) ||
	    memcmp(tag, expected_tag, sizeof(tag))) {
		fputs("AES-256-GCM self-test failed\n", stderr);
		return 1;
	}
	return 0;
}

static void quit_session(int peer, struct channel *channel)
{
	uint8_t envelope[FRAME_MAX];
	size_t length = seal_envelope(
		REQUEST, channel->session, channel->request_sequence++,
		"QUIT", envelope);

	send_frame(peer, envelope, length);
	expect_eof(peer);
	close(peer);
}

static int serve(const char *directory, const char *port_text,
		 const char *address_text)
{
	unsigned long parsed = strtoul(port_text, NULL, 10);
	unsigned short port;
	unsigned short shell_port;
	uint64_t previous = 0;
	uint8_t saved_shell[FRAME_MAX];
	size_t saved_shell_length = 0;
	int shell_server;
	int server;
	int peer;

	if (!parsed || parsed > 65535) {
		errno = EINVAL;
		fail("port");
	}
	server = listener(address_text, (unsigned short)parsed, &port, 1);
	shell_server = listener(address_text, 47024, &shell_port, 1);
	if (port != parsed || shell_port != 47024) {
		errno = EPROTO;
		fail("bound port");
	}
	marker(directory, "ready");

	{
		struct channel channel;
		uint8_t replay[FRAME_MAX];
		size_t replay_length;

		peer = accept_peer(server);
		previous = receive_hello(peer, previous, &channel);
		expect_reply(peer, &channel, "PING", "PONG");
		expect_reply(peer, &channel, "INFO", "INFO rootkitlings");
		expect_reply(peer, &channel, "MARK alpha_7", "MARKED alpha_7");
		expect_reply(peer, &channel, "UNKNOWN", "ERR");
		replay_length = seal_envelope(
			REQUEST, channel.session, channel.request_sequence - 1,
			"UNKNOWN", replay);
		send_frame(peer, replay, replay_length);
		expect_eof(peer);
		close(peer);
		marker(directory, "replay_rejected");
	}

	{
		struct channel channel;
		uint8_t tampered[FRAME_MAX];
		size_t length;
		char reply[FRAME_MAX];

		peer = accept_peer(server);
		previous = receive_hello(peer, previous, &channel);
		command(peer, &channel, "STATUS", reply);
		if (strncmp(reply, "STATUS ", 7)) {
			errno = EPROTO;
			fail("status reply");
		}
		length = seal_envelope(REQUEST, channel.session,
				       channel.request_sequence, "PING", tampered);
		tampered[length - 1] ^= 1;
		send_frame(peer, tampered, length);
		expect_eof(peer);
		close(peer);
		marker(directory, "tamper_rejected");
	}

	{
		struct channel channel;
		char reply[FRAME_MAX];
		uint32_t request_id;
		uint64_t cookie;
		int shell_peer;

		peer = accept_peer(server);
		previous = receive_hello(peer, previous, &channel);
		expect_reply(peer, &channel, "SHELL 47024", "ERR disabled");
		expect_no_peer(shell_server);
		expect_reply(peer, &channel, "ENABLE", "ENABLED");
		saved_shell_length = seal_envelope(
			REQUEST, channel.session, channel.request_sequence,
			"SHELL 47024", saved_shell);
		command(peer, &channel, "SHELL 47024", reply);
		parse_ticket(reply, &request_id, &cookie);
		shell_peer = accept_shell(shell_server, request_id, cookie);
		wait_shell_state(peer, &channel, request_id, "running", -115);
		expect_reply(peer, &channel, "SHELL 47024", "ERR busy");
		run_shell(shell_peer);
		wait_shell_state(peer, &channel, request_id, "exited", 0);
		marker(directory, "root_shell_complete");
		command(peer, &channel, "RUN", reply);
		if (strcmp(reply, "QUEUED") && strcmp(reply, "ERR busy")) {
			errno = EPROTO;
			fail("RUN reply");
		}
		expect_reply(peer, &channel, "DISABLE", "DISABLED");
		expect_reply(peer, &channel, "SHELL 47024", "ERR disabled");
		marker(directory, "control_complete");
		quit_session(peer, &channel);
		marker(directory, "quit_complete");
	}

	{
		struct channel channel;

		peer = accept_peer(server);
		previous = receive_hello(peer, previous, &channel);
		send_frame(peer, saved_shell, saved_shell_length);
		expect_eof(peer);
		close(peer);
		expect_no_peer(shell_server);
		marker(directory, "shell_replay_rejected");
	}

	{
		struct channel channel;
		uint8_t tampered[FRAME_MAX];
		size_t length;

		peer = accept_peer(server);
		previous = receive_hello(peer, previous, &channel);
		length = seal_envelope(REQUEST, channel.session, 1,
				       "SHELL 47024", tampered);
		tampered[length - 1] ^= 1;
		send_frame(peer, tampered, length);
		expect_eof(peer);
		close(peer);
		expect_no_peer(shell_server);
		marker(directory, "shell_tamper_rejected");
	}

	{
		struct channel channel;
		char reply[FRAME_MAX];
		uint32_t request_id;
		uint64_t cookie;
		int shell_peer;

		peer = accept_peer(server);
		previous = receive_hello(peer, previous, &channel);
		expect_reply(peer, &channel, "ENABLE", "ENABLED");
		command(peer, &channel, "SHELL 47025", reply);
		parse_ticket(reply, &request_id, &cookie);
		wait_shell_state(peer, &channel, request_id, "refused", -111);
		marker(directory, "shell_refused");

		command(peer, &channel, "SHELL 47024", reply);
		parse_ticket(reply, &request_id, &cookie);
		shell_peer = accept_shell(shell_server, request_id, cookie);
		wait_shell_state(peer, &channel, request_id, "timed-out", -110);
		drain_shell_eof(shell_peer);
		close(shell_peer);
		marker(directory, "shell_timeout");

		command(peer, &channel, "SHELL 47024", reply);
		parse_ticket(reply, &request_id, &cookie);
		shell_peer = accept_shell(shell_server, request_id, cookie);
		marker(directory, "fresh_sessions");
		marker(directory, "blocked_shell");
		expect_reply(peer, &channel, "UNHIDE MODULE", "OK");
		marker(directory, "uncloaked");
		marker(directory, "blocked_receive");
		expect_eof(peer);
		drain_shell_eof(shell_peer);
		close(shell_peer);
		close(peer);
	}

	close(shell_server);
	close(server);
	marker(directory, "eof");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "selftest"))
		return crypto_selftest();
	if (argc == 3 && !strcmp(argv[1], "reserve"))
		return reserve(argv[2]);
	if (argc == 6 && !strcmp(argv[1], "serve")) {
		int result;

		if (parse_key(argv[5])) {
			fputs("invalid key: expected 64 hexadecimal digits; "
			      "key must not be all zero\n", stderr);
			return 2;
		}
		result = serve(argv[2], argv[3], argv[4]);
		memset(key, 0, sizeof(key));
		return result;
	}
	fprintf(stderr, "usage: %s selftest | reserve FILE | "
		"serve DIRECTORY PORT ADDRESS KEY_HEX\n", argv[0]);
	return 2;
}
