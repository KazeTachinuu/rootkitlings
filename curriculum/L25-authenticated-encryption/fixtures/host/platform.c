#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "aead.h"

struct fake_tfm {
	u8 key[RK_AEAD_KEY_LENGTH];
	bool key_ready;
	bool auth_ready;
};

enum fake_operation { FAKE_NONE, FAKE_ENCRYPT, FAKE_DECRYPT };

struct fake_req {
	struct fake_tfm *tfm;
	u8 *buffer;
	u8 *iv;
	void *wait;
	unsigned int total;
	unsigned int ad;
	unsigned int crypt;
	unsigned int flags;
	enum fake_operation operation;
	bool submitted;
	bool completed;
};

static struct fake_req *active;
static int fail_at, submit_error, completion_error;
static unsigned int allocations, frees, request_allocations, request_frees;
static unsigned int clear_calls, clear_mask, wait_calls, violations;

void rk_test_reset(void)
{
	if (active)
		abort();
	fail_at = submit_error = completion_error = 0;
	allocations = frees = request_allocations = request_frees = 0;
	clear_calls = clear_mask = wait_calls = violations = 0;
}
void rk_test_fail(int point) { fail_at = point; }
void rk_test_async(int initial, int final)
{
	submit_error = initial;
	completion_error = final;
}
unsigned int rk_test_live(void)
{
	return allocations - frees + request_allocations - request_frees;
}
unsigned int rk_test_clears(void) { return clear_calls; }
unsigned int rk_test_clear_mask(void) { return clear_mask; }
unsigned int rk_test_waits(void) { return wait_calls; }
unsigned int rk_test_violations(void) { return violations; }

static void *f_alloc(void)
{
	struct fake_tfm *tfm;

	if (fail_at == 1)
		return (void *)(intptr_t)-ENOMEM;
	if (fail_at == 2)
		return NULL;
	tfm = calloc(1, sizeof(*tfm));
	if (tfm)
		allocations++;
	return tfm;
}
static bool f_is_error(const void *pointer)
{
	return (intptr_t)pointer < 0 && (intptr_t)pointer >= -4095;
}
static int f_pointer_error(const void *pointer)
{
	return (int)(intptr_t)pointer;
}
static unsigned int f_iv_size(void *pointer)
{
	(void)pointer;
	return fail_at == 3 ? 8 : RK_AEAD_NONCE_LENGTH;
}
static unsigned int f_auth_size(void *pointer)
{
	struct fake_tfm *tfm = pointer;

	if (!tfm->auth_ready)
		violations++;
	return fail_at == 6 ? 12 : RK_AEAD_TAG_LENGTH;
}
static int f_set_key(void *pointer, const u8 *key, unsigned int length)
{
	struct fake_tfm *tfm = pointer;

	if (fail_at == 4)
		return -EKEYREJECTED;
	if (length != RK_AEAD_KEY_LENGTH)
		return -EINVAL;
	memcpy(tfm->key, key, length);
	tfm->key_ready = true;
	return 0;
}
static int f_set_auth(void *pointer, unsigned int length)
{
	struct fake_tfm *tfm = pointer;

	if (fail_at == 5)
		return -EINVAL;
	if (length != RK_AEAD_TAG_LENGTH)
		return -EINVAL;
	tfm->auth_ready = true;
	return 0;
}
static void *f_req_alloc(void *pointer)
{
	struct fake_tfm *tfm = pointer;
	struct fake_req *request;

	if (fail_at == 7)
		return NULL;
	if (!tfm->key_ready || !tfm->auth_ready)
		violations++;
	request = calloc(1, sizeof(*request));
	if (request) {
		request->tfm = tfm;
		request_allocations++;
		active = request;
	}
	return request;
}
static void f_req_free(void *pointer)
{
	struct fake_req *request = pointer;

	if (request != active)
		violations++;
	if (request->submitted && !request->completed)
		violations++;
	free(request);
	request_frees++;
	active = NULL;
}
static void f_tfm_free(void *pointer)
{
	if (active && active->tfm == pointer)
		violations++;
	memset(pointer, 0, sizeof(struct fake_tfm));
	free(pointer);
	frees++;
}
static void f_sg(void *scatterlist, void *buffer, unsigned int length)
{
	(void)scatterlist;
	if (!active) {
		violations++;
		return;
	}
	active->buffer = buffer;
	active->total = length;
}
static void f_wait_init(void *wait)
{
	memset(wait, 0, sizeof(unsigned long) * 8);
}
static void f_callback(void *pointer, unsigned int flags, void *wait)
{
	struct fake_req *request = pointer;

	request->flags = flags;
	request->wait = wait;
	if (flags != (RK_AEAD_MAY_BACKLOG | RK_AEAD_MAY_SLEEP))
		violations++;
}
static void f_ad(void *pointer, unsigned int length)
{
	((struct fake_req *)pointer)->ad = length;
}
static void f_crypt(void *pointer, void *scatterlist, unsigned int length,
		    u8 *iv)
{
	struct fake_req *request = pointer;

	(void)scatterlist;
	request->crypt = length;
	request->iv = iv;
}

static bool request_is_valid(const struct fake_req *request,
			     enum fake_operation operation)
{
	unsigned int expected;

	if (!request || request != active || !request->tfm ||
	    !request->tfm->key_ready || !request->tfm->auth_ready ||
	    !request->buffer || !request->iv || !request->wait ||
	    request->flags != (RK_AEAD_MAY_BACKLOG | RK_AEAD_MAY_SLEEP))
		return false;
	if (operation == FAKE_DECRYPT && request->crypt < RK_AEAD_TAG_LENGTH)
		return false;
	expected = request->ad + request->crypt;
	if (operation == FAKE_ENCRYPT)
		expected += RK_AEAD_TAG_LENGTH;
	return request->total == expected;
}

static void make_tag(const struct fake_req *request,
		     u8 output[RK_AEAD_TAG_LENGTH], unsigned int plaintext_length)
{
	for (unsigned int i = 0; i < RK_AEAD_TAG_LENGTH; i++)
		output[i] = (u8)(request->tfm->key[i] ^
				request->tfm->key[i + RK_AEAD_TAG_LENGTH] ^
				request->iv[i % RK_AEAD_NONCE_LENGTH] ^ i);
	for (unsigned int i = 0; i < request->ad; i++)
		output[i % RK_AEAD_TAG_LENGTH] ^=
			(u8)(request->buffer[i] + i);
	for (unsigned int i = 0; i < plaintext_length; i++)
		output[i % RK_AEAD_TAG_LENGTH] ^=
			(u8)(request->buffer[request->ad + i] + 3 * i);
}

static int perform(struct fake_req *request)
{
	u8 received[RK_AEAD_TAG_LENGTH], expected[RK_AEAD_TAG_LENGTH];
	unsigned int length;

	if (request->operation == FAKE_ENCRYPT) {
		length = request->crypt;
		for (unsigned int i = 0; i < length; i++)
			request->buffer[request->ad + i] ^=
				(u8)(request->tfm->key[i % RK_AEAD_KEY_LENGTH] ^
				     request->iv[i % RK_AEAD_NONCE_LENGTH]);
		make_tag(request, expected, length);
		memcpy(request->buffer + request->ad + length, expected,
		       sizeof(expected));
		request->completed = true;
		return 0;
	}
	length = request->crypt - RK_AEAD_TAG_LENGTH;
	memcpy(received, request->buffer + request->ad + length,
	       sizeof(received));
	make_tag(request, expected, length);
	for (unsigned int i = 0; i < length; i++)
		request->buffer[request->ad + i] ^=
			(u8)(request->tfm->key[i % RK_AEAD_KEY_LENGTH] ^
			     request->iv[i % RK_AEAD_NONCE_LENGTH]);
	request->completed = true;
	return memcmp(received, expected, sizeof(received)) ? -EBADMSG : 0;
}

static int submit(void *pointer, enum fake_operation operation)
{
	struct fake_req *request = pointer;
	int result;

	if (!request_is_valid(request, operation)) {
		violations++;
		return -EPROTO;
	}
	request->operation = operation;
	request->submitted = true;
	if (submit_error == -EINPROGRESS || submit_error == -EBUSY)
		return submit_error;
	result = perform(request);
	return submit_error ? submit_error : result;
}
static int f_encrypt(void *pointer) { return submit(pointer, FAKE_ENCRYPT); }
static int f_decrypt(void *pointer) { return submit(pointer, FAKE_DECRYPT); }
static int f_wait(int initial, void *wait)
{
	int operation_result;

	wait_calls++;
	if (!active || active->wait != wait) {
		violations++;
		return -EPROTO;
	}
	if (initial != -EINPROGRESS && initial != -EBUSY)
		return initial;
	operation_result = perform(active);
	return completion_error ? completion_error : operation_result;
}
static void f_clear(void *pointer, size_t length)
{
	clear_calls++;
	if (length == RK_AEAD_WORK_MAX)
		clear_mask |= 1U;
	else if (length == RK_AEAD_NONCE_LENGTH)
		clear_mask |= 2U;
	else
		violations++;
	memset(pointer, 0, length);
}

const struct rk_aead_ops rk_kernel_aead_ops = {
	f_alloc, f_is_error, f_pointer_error, f_iv_size, f_auth_size,
	f_set_key, f_set_auth, f_req_alloc, f_req_free, f_tfm_free, f_sg,
	f_wait_init, f_callback, f_ad, f_crypt, f_encrypt, f_decrypt, f_wait,
	f_clear,
};
