// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/fprobe.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *proc_entry;
static atomic_long_t open_count = ATOMIC_LONG_INIT(0);

static int count_open(struct fprobe *fp, unsigned long entry_ip,
		      unsigned long ret_ip, struct ftrace_regs *regs,
		      void *entry_data)
{
	atomic_long_inc(&open_count);
	return 0;
}

static struct fprobe open_probe = {
	.entry_handler = count_open,
};

static int count_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%ld\n", atomic_long_read(&open_count));
	return 0;
}

static int __init fprobe_ling_init(void)
{
	int err;

	err = register_fprobe(&open_probe, "do_sys_openat2", NULL);
	if (err)
		return err;

	proc_entry = proc_create_single("rk_fprobe", 0444, NULL, count_show);
	if (!proc_entry) {
		unregister_fprobe(&open_probe);
		return -ENOMEM;
	}

	pr_info("rk: fprobe registered on do_sys_openat2\n");
	return 0;
}

static void __exit fprobe_ling_exit(void)
{
	proc_remove(proc_entry);
	unregister_fprobe(&open_probe);
	pr_info("rk: fprobe removed; %ld opens, %lu missed\n",
		atomic_long_read(&open_count), open_probe.nmissed);
}

module_init(fprobe_ling_init);
module_exit(fprobe_ling_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Function entry tracing with fprobe");
