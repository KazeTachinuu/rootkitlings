// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

#include "tripwire.h"

static struct proc_dir_entry *events_entry;

static int __init tripwire_init(void)
{
	int error;

	error = tripwire_monitor_start();
	if (error)
		return error;
	events_entry =
	    proc_create_single("rk_tripwire", 0444, NULL, tripwire_events_show);
	if (!events_entry) {
		tripwire_monitor_stop();
		return -ENOMEM;
	}
	return 0;
}

static void __exit tripwire_exit(void)
{
	proc_remove(events_entry);
	tripwire_monitor_stop();
}

module_init(tripwire_init);
module_exit(tripwire_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 4: file access monitor via do_sys_openat2");
