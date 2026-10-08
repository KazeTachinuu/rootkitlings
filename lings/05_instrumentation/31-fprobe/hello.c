// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/fprobe.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>

static struct proc_dir_entry *proc_entry;
static atomic_long_t read_count = ATOMIC_LONG_INIT(0);
static atomic_long_t open_count = ATOMIC_LONG_INIT(0);
static const char *io_symbols[] __maybe_unused = { "do_sys_openat2", "vfs_read" };

static int count_io(struct fprobe *fp, unsigned long entry_ip,
		      unsigned long ret_ip, struct ftrace_regs *regs,
		      void *entry_data)
{
	char symbol[KSYM_SYMBOL_LEN];

	sprint_symbol_no_offset(symbol, entry_ip);
	if (!strcmp(symbol, "vfs_read"))
		atomic_long_inc(&read_count);
	else if (!strcmp(symbol, "do_sys_openat2"))
		atomic_long_inc(&open_count);
	return 0;
}

static struct fprobe io_probe = {
	.entry_handler = count_io,
};

static int count_show(struct seq_file *m, void *v)
{
	seq_printf(m, "opens=%ld\nreads=%ld\nmissed=%lu\n",
		   atomic_long_read(&open_count), atomic_long_read(&read_count),
		   io_probe.nmissed);
	return 0;
}

static int __init fprobe_ling_init(void)
{
	int err;

	/* Register io_probe for every name in io_symbols. */
	err = -EINVAL;
	if (err)
		return err;

	proc_entry = proc_create_single("rk_fprobe", 0444, NULL, count_show);
	if (!proc_entry) {
		unregister_fprobe(&io_probe);
		return -ENOMEM;
	}

	pr_info("rk: fprobe registered on do_sys_openat2 and vfs_read\n");
	return 0;
}

static void __exit fprobe_ling_exit(void)
{
	proc_remove(proc_entry);
	/* Unregister io_probe before its handler disappears. */
	pr_info("rk: fprobe removed; opens=%ld reads=%ld missed=%lu\n",
		atomic_long_read(&open_count), atomic_long_read(&read_count),
		io_probe.nmissed);
}

module_init(fprobe_ling_init);
module_exit(fprobe_ling_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Multi-function entry tracing with fprobe");
