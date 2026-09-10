#ifndef ROOTKITLINGS_PROTOCOL_H
#define ROOTKITLINGS_PROTOCOL_H

#include <linux/types.h>

#define C2_HEARTBEAT    0x01
#define C2_CMD_REQUEST  0x02
#define C2_CMD_RESPONSE 0x03
#define C2_ERROR        0x04

#define C2_HEADER_SIZE  5
#define C2_MAX_DATA     4096
#define AES_BLOCK       16
#define AES_KEY_SIZE    16

struct c2_header {
	__u8  type;
	__be32 length;
} __attribute__((packed));

#endif
