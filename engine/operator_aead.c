// SPDX-License-Identifier: GPL-2.0
#include <arpa/inet.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	KEY_LENGTH = 32,
	NONCE_LENGTH = 12,
	TAG_LENGTH = 16,
	HEADER_LENGTH = KEY_LENGTH + NONCE_LENGTH + 4,
	INPUT_LIMIT = 4096,
};

static int read_exact(unsigned char *buffer, size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		size_t count = fread(buffer + offset, 1, length - offset, stdin);

		if (!count)
			return -1;
		offset += count;
	}
	return 0;
}

static int write_exact(const unsigned char *buffer, size_t length)
{
	return fwrite(buffer, 1, length, stdout) == length ? 0 : -1;
}

static int seal(EVP_CIPHER_CTX *context, const unsigned char *key,
		const unsigned char *nonce, const unsigned char *aad,
		int aad_length, const unsigned char *input, int input_length,
		unsigned char *output)
{
	int count = 0;
	int length = 0;

	if (EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
	    EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN,
				NONCE_LENGTH, NULL) != 1 ||
	    EVP_EncryptInit_ex(context, NULL, NULL, key, nonce) != 1 ||
	    EVP_EncryptUpdate(context, NULL, &count, aad, aad_length) != 1 ||
	    EVP_EncryptUpdate(context, output, &count, input, input_length) != 1)
		return -1;
	length = count;
	if (EVP_EncryptFinal_ex(context, output + length, &count) != 1)
		return -1;
	length += count;
	if (length != input_length ||
	    EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG, TAG_LENGTH,
				output + length) != 1)
		return -1;
	return length + TAG_LENGTH;
}

static int open_envelope(EVP_CIPHER_CTX *context, const unsigned char *key,
			 const unsigned char *nonce, const unsigned char *aad,
			 int aad_length, unsigned char *input, int input_length,
			 unsigned char *output)
{
	int ciphertext_length = input_length - TAG_LENGTH;
	int count = 0;
	int length = 0;

	if (ciphertext_length < 0 ||
	    EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), NULL,
			       NULL, NULL) != 1 ||
	    EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN,
				NONCE_LENGTH, NULL) != 1 ||
	    EVP_DecryptInit_ex(context, NULL, NULL, key, nonce) != 1 ||
	    EVP_DecryptUpdate(context, NULL, &count, aad, aad_length) != 1 ||
	    EVP_DecryptUpdate(context, output, &count, input,
			      ciphertext_length) != 1)
		return -1;
	length = count;
	if (EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG, TAG_LENGTH,
				input + ciphertext_length) != 1 ||
	    EVP_DecryptFinal_ex(context, output + length, &count) != 1)
		return -2;
	return length + count;
}

int main(int argc, char **argv)
{
	unsigned char header[HEADER_LENGTH];
	unsigned char input[INPUT_LIMIT * 2 + TAG_LENGTH];
	unsigned char output[INPUT_LIMIT + TAG_LENGTH];
	EVP_CIPHER_CTX *context = NULL;
	uint16_t encoded;
	int aad_length;
	int input_length;
	int output_length;
	int result = EXIT_FAILURE;

	if (argc != 2 ||
	    (strcmp(argv[1], "seal") != 0 && strcmp(argv[1], "open") != 0)) {
		fprintf(stderr, "usage: operator-aead seal|open\n");
		return EXIT_FAILURE;
	}
	if (read_exact(header, sizeof(header))) {
		fprintf(stderr, "operator crypto request is truncated\n");
		return EXIT_FAILURE;
	}
	memcpy(&encoded, header + KEY_LENGTH + NONCE_LENGTH, sizeof(encoded));
	aad_length = ntohs(encoded);
	memcpy(&encoded, header + KEY_LENGTH + NONCE_LENGTH + 2,
	       sizeof(encoded));
	input_length = ntohs(encoded);
	if (aad_length > INPUT_LIMIT ||
	    (strcmp(argv[1], "seal") == 0 && input_length > INPUT_LIMIT) ||
	    (strcmp(argv[1], "open") == 0 &&
	     input_length > INPUT_LIMIT + TAG_LENGTH) ||
	    read_exact(input, (size_t)aad_length + (size_t)input_length) ||
	    fgetc(stdin) != EOF) {
		fprintf(stderr, "operator crypto input is invalid\n");
		goto out;
	}
	context = EVP_CIPHER_CTX_new();
	if (!context) {
		fprintf(stderr, "OpenSSL context allocation failed\n");
		goto out;
	}
	if (strcmp(argv[1], "seal") == 0)
		output_length = seal(context, header, header + KEY_LENGTH,
				     input, aad_length, input + aad_length,
				     input_length, output);
	else
		output_length = open_envelope(
			context, header, header + KEY_LENGTH, input, aad_length,
			input + aad_length, input_length, output);
	if (output_length == -2) {
		fprintf(stderr, "AES-GCM authentication failed\n");
		goto out;
	}
	if (output_length < 0 || write_exact(output, (size_t)output_length)) {
		fprintf(stderr, "OpenSSL AES-GCM operation failed\n");
		goto out;
	}
	result = EXIT_SUCCESS;
out:
	EVP_CIPHER_CTX_free(context);
	OPENSSL_cleanse(header, sizeof(header));
	OPENSSL_cleanse(input, sizeof(input));
	OPENSSL_cleanse(output, sizeof(output));
	return result;
}
