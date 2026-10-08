// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static const unsigned char tag[] = {'r', 'o', 'o', 't'};
static struct proc_dir_entry *debug_entry;

static noinline __noclone u32 checksum(const unsigned char *data, size_t len)
{
	u8 sum = 0;
	size_t i;

	for (i = 0; i < len; i++)
		sum += data[i];

	return sum;
}

static int rk_show(struct seq_file *m, void *unused)
{
	seq_printf(m, "result=%d\n", checksum(tag, sizeof(tag)));
	return 0;
}

static int __init rk_init(void)
{
	debug_entry = proc_create_single("rk_debug", 0444, NULL, rk_show);
	return debug_entry ? 0 : -ENOMEM;
}

static void __exit rk_exit(void)
{
	proc_remove(debug_entry);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Debug a module function with GDB");
