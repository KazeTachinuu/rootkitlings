#ifndef RK_FIXTURE_NET_CHECKSUM_H
#define RK_FIXTURE_NET_CHECKSUM_H

#include <linux/skbuff.h>

void inet_proto_csum_replace2(__sum16 *sum, struct sk_buff *skb,
			      __be16 from, __be16 to, bool pseudohdr);

#endif
