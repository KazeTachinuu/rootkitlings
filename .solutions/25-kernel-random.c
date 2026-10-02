// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;
static u32 session_id;
static u8 auth_token[16];

static int session_show(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "session=%08x\ntoken=", session_id);
	for (i = 0; i < 16; i++)
		seq_printf(m, "%02x", auth_token[i]);
	seq_putc(m, '\n');
	return 0;
}

static int __init session_init(void)
{
	session_id = get_random_u32();
	get_random_bytes(auth_token, sizeof(auth_token));

	pr_info("rk: session id=%08x\n", session_id);

	entry = proc_create_single("rk_entropy", 0444, NULL, session_show);
	if (!entry)
		return -ENOMEM;

	return 0;
}

static void __exit session_exit(void)
{
	proc_remove(entry);
}

module_init(session_init);
module_exit(session_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel random number generation");
