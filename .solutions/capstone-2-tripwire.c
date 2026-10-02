// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>

static bool armed = true;
module_param(armed, bool, 0644);
MODULE_PARM_DESC(armed, "Enable file access logging");

static const char *watchlist[] = {
	"/etc/shadow",
	"/etc/passwd",
};

#define WATCH_COUNT (sizeof(watchlist) / sizeof(watchlist[0]))

struct trip_entry {
	uid_t uid;
	pid_t pid;
	char path[64];
	struct list_head node;
};

static LIST_HEAD(trip_log);
static DEFINE_SPINLOCK(trip_lock);
static struct proc_dir_entry *proc_entry;

static unsigned long kaddr(const char *name)
{
	struct kprobe kp = { .symbol_name = name };
	unsigned long addr;

	if (register_kprobe(&kp))
		return 0;
	addr = (unsigned long)kp.addr;
	unregister_kprobe(&kp);
	return addr;
}

static struct ftrace_ops trip_ops;

// do_sys_openat2(int dfd, const char __user *filename, struct open_how *how)
static void notrace trip_callback(unsigned long ip, unsigned long parent_ip,
				  struct ftrace_ops *ops,
				  struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);
	char path[64];
	struct trip_entry *e;
	long len;
	int i;

	if (!armed || !regs)
		return;

	len = strncpy_from_user(path, (const char __user *)regs->si,
				sizeof(path));
	if (len <= 0)
		return;

	for (i = 0; i < WATCH_COUNT; i++) {
		if (strcmp(path, watchlist[i]) == 0) {
			e = kmalloc(sizeof(*e), GFP_ATOMIC);
			if (!e)
				return;
			e->uid = current_uid().val;
			e->pid = current->pid;
			strscpy(e->path, path, sizeof(e->path));
			spin_lock(&trip_lock);
			list_add_tail(&e->node, &trip_log);
			spin_unlock(&trip_lock);
			pr_warn("rk: exec [tripwire] uid=%u pid=%d %s\n",
				e->uid, e->pid, e->path);
			return;
		}
	}
}

static ssize_t trip_read(struct file *f, char __user *buf,
			 size_t count, loff_t *pos)
{
	struct trip_entry *e;
	char *page;
	int len = 0;
	ssize_t ret;

	page = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	spin_lock(&trip_lock);
	list_for_each_entry(e, &trip_log, node)
		len += snprintf(page + len, PAGE_SIZE - len,
				"[tripwire] uid=%u pid=%d %s\n",
				e->uid, e->pid, e->path);
	spin_unlock(&trip_lock);

	ret = simple_read_from_buffer(buf, count, pos, page, len);
	kfree(page);
	return ret;
}

static const struct proc_ops trip_proc_ops = {
	.proc_read = trip_read,
};

static int __init tripwire_init(void)
{
	unsigned long addr;
	int err;

	addr = kaddr("do_sys_openat2");
	if (!addr) {
		pr_err("rk: do_sys_openat2 not found\n");
		return -ENOENT;
	}

	trip_ops.func = trip_callback;
	trip_ops.flags = FTRACE_OPS_FL_SAVE_REGS;

	err = ftrace_set_filter_ip(&trip_ops, addr, 0, 0);
	if (err)
		return err;
	err = register_ftrace_function(&trip_ops);
	if (err)
		return err;

	proc_entry = proc_create("rk_tripwire", 0444, NULL, &trip_proc_ops);
	if (!proc_entry) {
		unregister_ftrace_function(&trip_ops);
		return -ENOMEM;
	}

	pr_info("rk: tripwire armed\n");
	return 0;
}

static void __exit tripwire_exit(void)
{
	struct trip_entry *e, *tmp;

	proc_remove(proc_entry);
	unregister_ftrace_function(&trip_ops);

	list_for_each_entry_safe(e, tmp, &trip_log, node) {
		list_del(&e->node);
		kfree(e);
	}
	pr_info("rk: tripwire removed\n");
}

module_init(tripwire_init);
module_exit(tripwire_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 2: ftrace-based file access monitor");
