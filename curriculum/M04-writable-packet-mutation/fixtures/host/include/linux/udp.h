#ifndef RK_FIXTURE_LINUX_UDP_H
#define RK_FIXTURE_LINUX_UDP_H

#include <arpa/inet.h>
#include <linux/types.h>

struct udphdr {
	__be16 source;
	__be16 dest;
	__be16 len;
	__sum16 check;
};

#define CSUM_MANGLED_0 ((__sum16)0xffff)

#endif
