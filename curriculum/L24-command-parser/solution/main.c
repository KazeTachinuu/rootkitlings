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
	RK_CMD_INFO,
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

/* ------------------------------------------------------------------ */
/* Command table                                                       */
/* ------------------------------------------------------------------ */

struct rk_cmd_entry {
	const char *keyword;
	size_t keyword_len;
	enum rk_cmd_kind kind;
	bool has_arg;
};

/* Prefixed commands: keyword includes the trailing space before the argument.
 * Bare commands: keyword is the full match (no argument). */
static const struct rk_cmd_entry cmd_table[] = {
	/* Prefixed commands (with arguments) */
	{ "EXEC ",            5, RK_CMD_EXEC,           true  },
	{ "DOWNLOAD ",        9, RK_CMD_DOWNLOAD,       true  },
	/* Bare keywords (no arguments) */
	{ "PING",             4, RK_CMD_PING,           false },
	{ "STATUS",           6, RK_CMD_STATUS,         false },
	{ "SHELL",            5, RK_CMD_SHELL,          false },
	{ "INFO",             4, RK_CMD_INFO,           false },
};

/* ------------------------------------------------------------------ */
/* Command parser                                                      */
/* ------------------------------------------------------------------ */

/* Parse a text command from a decrypted buffer into a structured command. */
static int __maybe_unused rk_command_parse(const char *buf, size_t len,
					   struct rk_command *cmd)
{
	int i;

	cmd->kind = RK_CMD_UNKNOWN;
	cmd->arg[0] = '\0';

	len = rk_strip(buf, len);
	if (!len)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(cmd_table); i++) {
		const struct rk_cmd_entry *e = &cmd_table[i];

		if (e->has_arg) {
			if (len >= e->keyword_len &&
			    memcmp(buf, e->keyword, e->keyword_len) == 0) {
				cmd->kind = e->kind;
				rk_copy_arg(cmd, buf + e->keyword_len,
					    len - e->keyword_len);
				return 0;
			}
		} else {
			if (len == e->keyword_len &&
			    memcmp(buf, e->keyword, e->keyword_len) == 0) {
				cmd->kind = e->kind;
				return 0;
			}
		}
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

static int __maybe_unused rk_command_dispatch(struct rk_command *cmd)
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
	case RK_CMD_INFO:
		return rk_send_response("INFO OK");
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
