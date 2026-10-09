// SPDX-License-Identifier: GPL-2.0
#include <linux/limits.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <net/checksum.h>

#include "udp_redirect.h"
#include "writable.h"

enum rk_udp_redirect_result
rk_redirect_ipv4_udp(struct sk_buff *skb, const struct rk_udp_view *view,
		     unsigned int match_port, unsigned int replacement_port)
{
	struct udphdr before;
	struct udphdr *udp;
	unsigned int udp_offset;
	unsigned int writable_length;
	int network_offset;
	__be16 replacement;

	if (!skb || !view || !match_port || match_port > U16_MAX ||
	    !replacement_port || replacement_port > U16_MAX ||
	    match_port == replacement_port)
		return RK_UDP_REDIRECT_INVALID_ARGUMENT;
	if (view->source_port > U16_MAX ||
	    view->destination_port != match_port ||
	    view->datagram_length < sizeof(struct udphdr) ||
	    view->datagram_offset > SIZE_MAX - sizeof(struct udphdr) ||
	    view->payload_offset !=
		view->datagram_offset + sizeof(struct udphdr) ||
	    view->payload_length !=
		view->datagram_length - sizeof(struct udphdr) ||
	    view->datagram_offset > UINT_MAX ||
	    view->datagram_length > UINT_MAX)
		return RK_UDP_REDIRECT_INVALID_ARGUMENT;
	if (skb->data_len > skb->len)
		return RK_UDP_REDIRECT_INVALID_GEOMETRY;

	network_offset = skb_network_offset(skb);
	if (network_offset < 0 ||
	    view->datagram_offset > UINT_MAX - (unsigned int)network_offset)
		return RK_UDP_REDIRECT_INVALID_GEOMETRY;
	udp_offset = (unsigned int)network_offset + view->datagram_offset;
	if (udp_offset > INT_MAX || udp_offset > skb->len ||
	    sizeof(struct udphdr) > skb->len - udp_offset ||
	    view->datagram_length > skb->len - udp_offset)
		return RK_UDP_REDIRECT_INVALID_GEOMETRY;
	writable_length = udp_offset + sizeof(struct udphdr);
	if (skb_copy_bits(skb, udp_offset, &before, sizeof(before)))
		return RK_UDP_REDIRECT_INVALID_GEOMETRY;

	if (skb_nfct(skb) || secpath_exists(skb))
		return RK_UDP_REDIRECT_UNSUPPORTED_STATE;
	if (skb_is_gso(skb) && skb->ip_summed != CHECKSUM_PARTIAL)
		return RK_UDP_REDIRECT_UNSUPPORTED_STATE;
	if (skb->ip_summed == CHECKSUM_PARTIAL &&
	    (udp_offset > INT_MAX || skb_csum_is_sctp(skb) ||
	     skb_checksum_start_offset(skb) != (int)udp_offset ||
	     skb->csum_offset != offsetof(struct udphdr, check)))
		return RK_UDP_REDIRECT_UNSUPPORTED_STATE;

	if (rk_skb_make_writable(skb, writable_length))
		return RK_UDP_REDIRECT_NOT_WRITABLE;

	network_offset = skb_network_offset(skb);
	if (network_offset < 0 ||
	    view->datagram_offset > UINT_MAX - (unsigned int)network_offset)
		return RK_UDP_REDIRECT_INVALID_GEOMETRY;
	udp_offset = (unsigned int)network_offset + view->datagram_offset;
	if (udp_offset > skb_headlen(skb) ||
	    sizeof(struct udphdr) > skb_headlen(skb) - udp_offset)
		return RK_UDP_REDIRECT_INVALID_GEOMETRY;
	udp = (struct udphdr *)(skb->data + udp_offset);
	if (udp->source != before.source || udp->dest != before.dest ||
	    udp->len != before.len || ntohs(udp->source) != view->source_port ||
	    ntohs(udp->dest) != view->destination_port ||
	    ntohs(udp->len) != view->datagram_length)
		return RK_UDP_REDIRECT_STALE_VIEW;

	replacement = htons((u16)replacement_port);
	if (udp->check) {
		inet_proto_csum_replace2(&udp->check, skb, udp->dest,
					 replacement, false);
		if (!udp->check)
			udp->check = CSUM_MANGLED_0;
	} else {
		skb_checksum_complete_unset(skb);
	}
	udp->dest = replacement;
	skb_clear_hash(skb);
	return RK_UDP_REDIRECT_OK;
}
