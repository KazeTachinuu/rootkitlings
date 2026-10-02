#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "calc.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Multi-file module exercise");

static int rk_show(struct seq_file *m, void *v)
{
	seq_printf(m, "add=%d mul=%d\n", rk_add(3, 4), rk_mul(3, 4));
	return 0;
}

static int __init rk_init(void)
{
	proc_create_single("rk_calc", 0444, NULL, rk_show);
	return 0;
}

static void __exit rk_exit(void)
{
	remove_proc_entry("rk_calc", NULL);
}

module_init(rk_init);
module_exit(rk_exit);
