/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CMDS_H
#define CMDS_H

#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/types.h>

#define CMD_ADD   _IOW('H', 1, char[64])
#define CMD_CLEAR _IO('H', 2)
#define CMD_EXEC  _IO('H', 3)

struct cmd_entry {
	char text[64];
	char output[256];
	int result;
	bool executed;
	struct list_head node;
};

int cmds_add(const char *text);
void cmds_exec_all(void);
void cmds_clear(void);
int cmds_format(char *buf, size_t size);

#endif
