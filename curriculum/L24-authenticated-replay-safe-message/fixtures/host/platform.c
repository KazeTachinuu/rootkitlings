#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "auth_platform.h"

struct sha256_state {
	uint32_t state[8];
	uint64_t bits;
	u8 block[64];
	size_t used;
};

struct rk_host_context {
	u8 key[64];
	size_t key_length;
	pthread_mutex_t lock;
	int locked;
};

static int fail_point;
static unsigned int live_contexts;
static unsigned int erased_contexts;
static _Thread_local u8 *pending_digest;
static _Thread_local struct rk_host_context *pending_context;
static _Thread_local unsigned int tag_comparisons;
static _Thread_local size_t tag_bytes;
static _Thread_local size_t tag_length;
static _Thread_local bool tag_compared_while_locked;
static _Thread_local bool override_tag_result;
static _Thread_local bool forced_tag_result;

static uint32_t rotate_right(uint32_t value, unsigned int count)
{
	return (value >> count) | (value << (32 - count));
}

static void sha256_compress(struct sha256_state *state, const u8 block[64])
{
	static const uint32_t constants[64] = {
	    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
	    0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
	    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
	    0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
	    0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
	    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
	    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
	    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
	    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
	};
	uint32_t words[64];
	uint32_t a, b, c, d, e, f, g, h;
	unsigned int index;

	for (index = 0; index < 16; index++)
		words[index] = (uint32_t)block[index * 4] << 24 |
			       (uint32_t)block[index * 4 + 1] << 16 |
			       (uint32_t)block[index * 4 + 2] << 8 |
			       block[index * 4 + 3];
	for (index = 16; index < 64; index++) {
		uint32_t first = rotate_right(words[index - 15], 7) ^
				 rotate_right(words[index - 15], 18) ^
				 (words[index - 15] >> 3);
		uint32_t second = rotate_right(words[index - 2], 17) ^
				  rotate_right(words[index - 2], 19) ^
				  (words[index - 2] >> 10);

		words[index] =
		    words[index - 16] + first + words[index - 7] + second;
	}
	a = state->state[0];
	b = state->state[1];
	c = state->state[2];
	d = state->state[3];
	e = state->state[4];
	f = state->state[5];
	g = state->state[6];
	h = state->state[7];
	for (index = 0; index < 64; index++) {
		uint32_t choice = (e & f) ^ (~e & g);
		uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
		uint32_t upper_a = rotate_right(a, 2) ^ rotate_right(a, 13) ^
				   rotate_right(a, 22);
		uint32_t upper_e = rotate_right(e, 6) ^ rotate_right(e, 11) ^
				   rotate_right(e, 25);
		uint32_t first =
		    h + upper_e + choice + constants[index] + words[index];
		uint32_t second = upper_a + majority;

		h = g;
		g = f;
		f = e;
		e = d + first;
		d = c;
		c = b;
		b = a;
		a = first + second;
	}
	state->state[0] += a;
	state->state[1] += b;
	state->state[2] += c;
	state->state[3] += d;
	state->state[4] += e;
	state->state[5] += f;
	state->state[6] += g;
	state->state[7] += h;
}

static void sha256_init(struct sha256_state *state)
{
	static const uint32_t initial[8] = {
	    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
	};

	memset(state, 0, sizeof(*state));
	memcpy(state->state, initial, sizeof(initial));
}

static void sha256_update(struct sha256_state *state, const u8 *data,
			  size_t length)
{
	while (length) {
		size_t count = sizeof(state->block) - state->used;

		if (count > length)
			count = length;
		memcpy(state->block + state->used, data, count);
		state->used += count;
		state->bits += (uint64_t)count * 8;
		data += count;
		length -= count;
		if (state->used == sizeof(state->block)) {
			sha256_compress(state, state->block);
			state->used = 0;
		}
	}
}

static void sha256_final(struct sha256_state *state, u8 digest[32])
{
	uint64_t bits = state->bits;
	unsigned int index;

	state->block[state->used++] = 0x80;
	if (state->used > 56) {
		memset(state->block + state->used, 0,
		       sizeof(state->block) - state->used);
		sha256_compress(state, state->block);
		state->used = 0;
	}
	memset(state->block + state->used, 0, 56 - state->used);
	for (index = 0; index < 8; index++)
		state->block[63 - index] = (u8)(bits >> (index * 8));
	sha256_compress(state, state->block);
	for (index = 0; index < 8; index++) {
		digest[index * 4] = (u8)(state->state[index] >> 24);
		digest[index * 4 + 1] = (u8)(state->state[index] >> 16);
		digest[index * 4 + 2] = (u8)(state->state[index] >> 8);
		digest[index * 4 + 3] = (u8)state->state[index];
	}
	memset(state, 0, sizeof(*state));
}

static void host_hmac(const u8 *key, size_t key_length, const u8 *data,
		      size_t length, u8 digest[RK_AUTH_DIGEST_LENGTH])
{
	struct sha256_state state;
	u8 block[64] = {0};
	u8 inner[RK_AUTH_DIGEST_LENGTH];
	u8 pad[64];
	size_t index;

	if (key_length > sizeof(block)) {
		sha256_init(&state);
		sha256_update(&state, key, key_length);
		sha256_final(&state, block);
	} else {
		memcpy(block, key, key_length);
	}
	for (index = 0; index < sizeof(pad); index++)
		pad[index] = block[index] ^ 0x36;
	sha256_init(&state);
	sha256_update(&state, pad, sizeof(pad));
	sha256_update(&state, data, length);
	sha256_final(&state, inner);
	for (index = 0; index < sizeof(pad); index++)
		pad[index] = block[index] ^ 0x5c;
	sha256_init(&state);
	sha256_update(&state, pad, sizeof(pad));
	sha256_update(&state, inner, sizeof(inner));
	sha256_final(&state, digest);
	memset(block, 0, sizeof(block));
	memset(inner, 0, sizeof(inner));
	memset(pad, 0, sizeof(pad));
}

void rk_test_fail_at(int point)
{
	fail_point = point;
}

unsigned int rk_test_live_contexts(void)
{
	return live_contexts;
}

unsigned int rk_test_erased_contexts(void)
{
	return erased_contexts;
}

bool rk_test_digest_was_cleared(void)
{
	return pending_digest == NULL;
}

void rk_test_force_tag_result(bool enabled, bool result)
{
	override_tag_result = enabled;
	forced_tag_result = result;
	tag_comparisons = 0;
	tag_bytes = 0;
	tag_length = 0;
	tag_compared_while_locked = false;
}

unsigned int rk_test_tag_comparisons(void)
{
	return tag_comparisons;
}

size_t rk_test_tag_bytes(void)
{
	return tag_bytes;
}

size_t rk_test_tag_length(void)
{
	return tag_length;
}

bool rk_test_tag_compared_while_locked(void)
{
	return tag_compared_while_locked;
}

int rk_platform_auth_create(void **context, const u8 *key, size_t key_length)
{
	struct rk_host_context *created;

	if (!context || !key || key_length > sizeof(created->key))
		return -EINVAL;
	if (fail_point == 1)
		return -ENOMEM;
	created = calloc(1, sizeof(*created));
	if (!created)
		return -ENOMEM;
	if (fail_point == 2) {
		memset(created, 0, sizeof(*created));
		free(created);
		return -EKEYREJECTED;
	}
	memcpy(created->key, key, key_length);
	created->key_length = key_length;
	if (pthread_mutex_init(&created->lock, NULL)) {
		memset(created, 0, sizeof(*created));
		free(created);
		return -ENOMEM;
	}
	live_contexts++;
	*context = created;
	return 0;
}

void rk_platform_auth_destroy(void *context)
{
	struct rk_host_context *owned = context;
	u8 *bytes;
	size_t index;
	int erased = 1;

	if (!owned)
		return;
	pthread_mutex_destroy(&owned->lock);
	memset(owned, 0, sizeof(*owned));
	bytes = (u8 *)owned;
	for (index = 0; index < sizeof(*owned); index++)
		if (bytes[index])
			erased = 0;
	if (erased)
		erased_contexts++;
	live_contexts--;
	free(owned);
}

int rk_platform_hmac(void *context, const u8 *data, size_t length,
		     u8 digest[RK_AUTH_DIGEST_LENGTH])
{
	struct rk_host_context *owned = context;

	if (owned->locked != 1)
		return -EDEADLK;
	pending_digest = digest;
	pending_context = owned;
	if (fail_point == 3)
		return -EIO;
	host_hmac(owned->key, owned->key_length, data, length, digest);
	return 0;
}

int rk_platform_sign_once(const u8 *key, size_t key_length, const u8 *data,
			  size_t length, u8 digest[RK_AUTH_DIGEST_LENGTH])
{
	void *context = NULL;
	int error;

	error = rk_platform_auth_create(&context, key, key_length);
	if (error)
		return error;
	rk_platform_lock(context);
	error = rk_platform_hmac(context, data, length, digest);
	rk_platform_unlock(context);
	/* sign_once returns this digest; only learner temporaries must be
	 * cleared. */
	pending_digest = NULL;
	pending_context = NULL;
	rk_platform_auth_destroy(context);
	return error;
}

bool rk_platform_tags_equal(const u8 *left, const u8 *right, size_t length)
{
	u8 difference = 0;
	size_t index;

	tag_comparisons++;
	tag_length = length;
	tag_compared_while_locked = pending_context &&
		pending_context->locked == 1 && left == pending_digest;
	for (index = 0; index < length; index++) {
		difference |= left[index] ^ right[index];
		tag_bytes++;
	}
	if (override_tag_result)
		return forced_tag_result;
	return difference == 0;
}

void rk_platform_lock(void *context)
{
	struct rk_host_context *owned = context;

	pthread_mutex_lock(&owned->lock);
	owned->locked = 1;
}

void rk_platform_unlock(void *context)
{
	struct rk_host_context *owned = context;

	owned->locked = 0;
	pthread_mutex_unlock(&owned->lock);
}

void rk_platform_clear(void *buffer, size_t length)
{
	volatile u8 *bytes = buffer;
	size_t original_length = length;

	while (length--)
		*bytes++ = 0;
	if (buffer == pending_digest &&
	    original_length == RK_AUTH_DIGEST_LENGTH) {
		pending_digest = NULL;
		pending_context = NULL;
	}
}
