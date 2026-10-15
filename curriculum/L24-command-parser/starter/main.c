// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#define RK_ARG_MAX	128

/* ------------------------------------------------------------------ */
/* Command kinds                                                       */
/* ------------------------------------------------------------------ */

/*
 * TODO: Define enum rk_cmd_kind with all command kinds:
 *
 *   RK_CMD_PING, RK_CMD_STATUS, RK_CMD_SHELL, RK_CMD_EXEC,
 *   RK_CMD_DOWNLOAD, RK_CMD_INFO, RK_CMD_UNKNOWN
 */

/*
 * TODO: Define struct rk_command with:
 *   - enum rk_cmd_kind kind
 *   - char arg[RK_ARG_MAX]
 */

/* ------------------------------------------------------------------ */
/* Parsing helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_copy_arg(cmd, src, len).
 *
 * Copy at most RK_ARG_MAX-1 bytes from src into cmd->arg.
 * Always null-terminate.
 */

/*
 * TODO: Implement rk_strip(buf, len).
 *
 * Strip trailing newlines, carriage returns, and spaces.
 * Return the new length.
 */

/*
 * TODO: Implement rk_prefix(buf, len, prefix, plen).
 *
 * Return true if buf starts with the given prefix.
 */

/*
 * TODO: Implement rk_exact(buf, len, word).
 *
 * Return true if buf (of given len) exactly matches word.
 */

/* ------------------------------------------------------------------ */
/* Command parser                                                      */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_command_parse(buf, len, cmd).
 *
 * Parse a text command from a decrypted byte buffer.
 *
 * 1. Initialize cmd->kind = RK_CMD_UNKNOWN, cmd->arg[0] = '\0'.
 * 2. Strip trailing whitespace.
 * 3. Match prefixed commands (with arguments):
 *    "EXEC "     -> RK_CMD_EXEC,     arg = remainder
 *    "DOWNLOAD " -> RK_CMD_DOWNLOAD, arg = remainder
 * 4. Match bare keywords:
 *    "PING", "STATUS", "SHELL", "INFO"
 * 5. Default: RK_CMD_UNKNOWN (already set).
 *
 * Return 0 on success, -EINVAL if the buffer is empty after stripping.
 */

/* ------------------------------------------------------------------ */
/* Command dispatch                                                    */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_command_dispatch(cmd).
 *
 * Switch on cmd->kind and call the appropriate handler.
 * For this lesson, stub each handler to return 0.
 * Return -EINVAL for impossible cases.
 *
 * For RK_CMD_UNKNOWN, the response should be "ERR unknown command".
 */

/* ------------------------------------------------------------------ */
/* Module lifecycle                                                    */
/* ------------------------------------------------------------------ */

static int __init rk_init(void)
{
	return 0;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
