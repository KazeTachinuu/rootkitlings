#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GDB debugging exercise");

static int compute(int base, int factor, int offset)
{
	return base * factor + offset;
}

static int rk_buggy_show(struct seq_file *m, void *v)
{
	int result = compute(8, 5, 2);

	seq_printf(m, "result=%d\n", result);
	return 0;
}

static int __init rk_init(void)
{
	proc_create_single("rk_debug", 0444, NULL, rk_buggy_show);
	return 0;
}

static void __exit rk_exit(void)
{
	remove_proc_entry("rk_debug", NULL);
}

module_init(rk_init);
module_exit(rk_exit);
