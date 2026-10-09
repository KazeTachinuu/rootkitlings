#ifndef RK_FIXTURE_LINUX_SKBUFF_H
#define RK_FIXTURE_LINUX_SKBUFF_H

#include <linux/types.h>
#include <string.h>

enum {
	CHECKSUM_NONE,
	CHECKSUM_UNNECESSARY,
	CHECKSUM_COMPLETE,
	CHECKSUM_PARTIAL,
};

struct sk_buff {
	unsigned char *data;
	unsigned char *wire;
	unsigned int len;
	unsigned int data_len;
	unsigned int head_length;
	unsigned int ip_summed;
	unsigned int csum_not_inet;
	unsigned int csum_start;
	unsigned int csum_offset;
	unsigned int hash;
	unsigned int sw_hash;
	unsigned int l4_hash;
	int network_offset;
	bool gso;
	bool nfct;
	bool secpath;
};

static inline int skb_network_offset(const struct sk_buff *skb)
{
	return skb->network_offset;
}

static inline unsigned int skb_headlen(const struct sk_buff *skb)
{
	return skb->head_length;
}

static inline int skb_copy_bits(const struct sk_buff *skb, int offset,
				void *to, int length)
{
	if (offset < 0 || length < 0 || (unsigned int)offset > skb->len ||
	    (unsigned int)length > skb->len - (unsigned int)offset)
		return -1;
	memcpy(to, skb->wire + offset, (size_t)length);
	return 0;
}

static inline bool skb_is_gso(const struct sk_buff *skb)
{
	return skb->gso;
}

static inline void *skb_nfct(const struct sk_buff *skb)
{
	return skb->nfct ? (void *)skb : NULL;
}

static inline int secpath_exists(const struct sk_buff *skb)
{
	return skb->secpath;
}

static inline int skb_checksum_start_offset(const struct sk_buff *skb)
{
	return (int)skb->csum_start;
}

static inline void skb_checksum_complete_unset(struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;
}

static inline void skb_clear_hash(struct sk_buff *skb)
{
	skb->hash = 0;
	skb->sw_hash = 0;
	skb->l4_hash = 0;
}

#endif
