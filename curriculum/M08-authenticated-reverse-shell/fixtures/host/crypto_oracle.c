#include <errno.h>
#include <openssl/evp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "secure_control.h"

static int forced_error;
static unsigned int clear_calls;
static unsigned int clear_failures;
static unsigned int clear_12;
static unsigned int clear_30;
static unsigned int clear_64;

void rk_test_crypto_fail(int error)
{
	forced_error = error;
}

void rk_test_clear_reset(void)
{
	clear_calls = 0;
	clear_failures = 0;
	clear_12 = 0;
	clear_30 = 0;
	clear_64 = 0;
}

unsigned int rk_test_clear_calls(void)
{
	return clear_calls;
}

unsigned int rk_test_clear_failures(void)
{
	return clear_failures;
}

unsigned int rk_test_clear_length(size_t length)
{
	if (length == 12)
		return clear_12;
	if (length == 30)
		return clear_30;
	if (length == 64)
		return clear_64;
	return 0;
}

void rk_secure_clear(void *buffer, size_t length)
{
	volatile u8 *bytes = buffer;
	size_t index;

	clear_calls++;
	if (length == 12)
		clear_12++;
	else if (length == 30)
		clear_30++;
	else if (length == 64)
		clear_64++;
	for (index = 0; index < length; index++)
		bytes[index] = 0;
	for (index = 0; index < length; index++)
		if (bytes[index])
			clear_failures++;
}

static int rk_openssl(bool decrypt, const u8 *key, const u8 *nonce,
		      const u8 *aad, size_t aad_length, const u8 *input,
		      size_t input_length, u8 *output, size_t output_capacity,
		      size_t *output_length)
{
	EVP_CIPHER_CTX *context = NULL;
	int count = 0;
	int total = 0;
	int error = -EIO;
	size_t payload_length;

	if (!key || !nonce || !aad || !input || !output || !output_length)
		return -EINVAL;
	if (forced_error) {
		if (output_capacity)
			memset(output, 0x6d, output_capacity);
		*output_length = output_capacity;
		error = forced_error;
		forced_error = 0;
		return error;
	}
	if (decrypt) {
		if (input_length < RK_AEAD_TAG_LENGTH)
			return -EMSGSIZE;
		payload_length = input_length - RK_AEAD_TAG_LENGTH;
	} else {
		payload_length = input_length;
	}
	if (output_capacity < payload_length +
			(!decrypt ? RK_AEAD_TAG_LENGTH : 0))
		return -ENOSPC;
	context = EVP_CIPHER_CTX_new();
	if (!context)
		return -ENOMEM;
	if (EVP_CipherInit_ex(context, EVP_aes_256_gcm(), NULL, NULL, NULL,
			      decrypt ? 0 : 1) != 1 ||
	    EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN,
				RK_AEAD_NONCE_LENGTH, NULL) != 1 ||
	    EVP_CipherInit_ex(context, NULL, NULL, key, nonce,
			      decrypt ? 0 : 1) != 1 ||
	    EVP_CipherUpdate(context, NULL, &count, aad, (int)aad_length) != 1)
		goto out;
	if (decrypt && EVP_CIPHER_CTX_ctrl(
			       context, EVP_CTRL_GCM_SET_TAG, RK_AEAD_TAG_LENGTH,
			       (void *)(input + payload_length)) != 1)
		goto out;
	if (EVP_CipherUpdate(context, output, &count, input,
			     (int)payload_length) != 1)
		goto out;
	total = count;
	if (EVP_CipherFinal_ex(context, output + total, &count) != 1) {
		error = decrypt ? -EBADMSG : -EIO;
		goto out;
	}
	total += count;
	if (!decrypt && EVP_CIPHER_CTX_ctrl(
				context, EVP_CTRL_GCM_GET_TAG, RK_AEAD_TAG_LENGTH,
				output + total) != 1)
		goto out;
	*output_length = (size_t)total + (!decrypt ? RK_AEAD_TAG_LENGTH : 0);
	error = 0;
out:
	EVP_CIPHER_CTX_free(context);
	return error;
}

int rk_aead_seal(const u8 *key, const u8 *nonce, const u8 *aad,
		 size_t aad_length, const u8 *plaintext, size_t plaintext_length,
		 u8 *sealed, size_t sealed_capacity, size_t *sealed_length)
{
	return rk_openssl(false, key, nonce, aad, aad_length, plaintext,
			  plaintext_length, sealed, sealed_capacity, sealed_length);
}

int rk_aead_open(const u8 *key, const u8 *nonce, const u8 *aad,
		 size_t aad_length, const u8 *sealed, size_t sealed_length,
		 u8 *plaintext, size_t plaintext_capacity, size_t *plaintext_length)
{
	return rk_openssl(true, key, nonce, aad, aad_length, sealed,
			  sealed_length, plaintext, plaintext_capacity,
			  plaintext_length);
}

static void rk_write_be64(u8 *bytes, uint64_t value)
{
	int index;

	for (index = 7; index >= 0; index--) {
		bytes[index] = (u8)value;
		value >>= 8;
	}
}

int rk_test_make_envelope_version(const u8 *key, u8 version, u8 kind,
				  uint64_t session, uint64_t sequence,
				  const u8 *plaintext, size_t plaintext_length,
				  u8 *envelope, size_t capacity, size_t *used)
{
	u8 nonce[RK_AEAD_NONCE_LENGTH];
	uint32_t counter;
	size_t sealed_length = 0;
	int error;

	if (capacity < RK_SECURE_HEADER_LENGTH + plaintext_length +
			RK_AEAD_TAG_LENGTH)
		return -ENOSPC;
	envelope[0] = version;
	envelope[1] = kind;
	rk_write_be64(envelope + 2, session);
	rk_write_be64(envelope + 10, sequence);
	rk_write_be64(nonce, session);
	counter = (uint32_t)(sequence << 1) | (kind == RK_SECURE_REPLY);
	nonce[8] = (u8)(counter >> 24);
	nonce[9] = (u8)(counter >> 16);
	nonce[10] = (u8)(counter >> 8);
	nonce[11] = (u8)counter;
	error = rk_openssl(false, key, nonce, envelope, RK_SECURE_HEADER_LENGTH,
			   plaintext, plaintext_length,
			   envelope + RK_SECURE_HEADER_LENGTH,
			   capacity - RK_SECURE_HEADER_LENGTH,
			   &sealed_length);
	if (!error)
		*used = RK_SECURE_HEADER_LENGTH + sealed_length;
	return error;
}

int rk_test_make_envelope(const u8 *key, u8 kind, uint64_t session,
			  uint64_t sequence, const u8 *plaintext,
			  size_t plaintext_length, u8 *envelope,
			  size_t capacity, size_t *used)
{
	return rk_test_make_envelope_version(key, RK_SECURE_VERSION, kind,
					     session, sequence, plaintext,
					     plaintext_length, envelope,
					     capacity, used);
}
