// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

MODULE_LICENSE("GPL");

static const unsigned char tag[] = { 'r', 'o', 'o', 't' };

static noinline __attribute__((noclone))
int checksum(const unsigned char *data, int len)
{
	int sum = 0;
	int i;

	for (i = 0; i < len; i++)
		sum += data[i];

	return sum;
}

static int rk_show(struct seq_file *m, void *v)
{
	seq_printf(m, "result=%d\n", checksum(tag, sizeof(tag)));
	return 0;
}

static int __init rk_init(void)
{
	proc_create_single("rk_debug", 0444, NULL, rk_show);
	return 0;
}

static void __exit rk_exit(void)
{
	remove_proc_entry("rk_debug", NULL);
}

module_init(rk_init);
module_exit(rk_exit);
