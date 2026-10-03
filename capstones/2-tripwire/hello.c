// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

static bool armed = true;
module_param(armed, bool, 0644);
MODULE_PARM_DESC(armed, "Enable file access logging");

static const char *watchlist[] = {
	"/etc/shadow",
	"/etc/passwd",
};

#define WATCH_COUNT ARRAY_SIZE(watchlist)

struct trip_entry {
	uid_t uid;
	pid_t pid;
	char path[64];
	struct list_head node;
};

static LIST_HEAD(trip_log);
static DEFINE_SPINLOCK(trip_lock);
static struct proc_dir_entry *proc_entry;

static long copy_user_string_nofault(char *dst, const char __user *src,
				     size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (copy_from_user_nofault(&dst[i], src + i, 1))
			return -EFAULT;
		if (dst[i] == '\0')
			return i + 1;
	}
	return size;
}

// do_sys_openat2(int dfd, const char __user *filename, struct open_how *how)
static int pre_do_sys_open(struct kprobe *p, struct pt_regs *regs)
{
	const char __user *uname = (const char __user *)regs->si;
	struct trip_entry *e;
	char buf[64];
	long len;
	int i;

	if (!armed || !uname)
		return 0;

	len = copy_user_string_nofault(buf, uname, sizeof(buf));
	if (len <= 0 || len >= (long)sizeof(buf))
		return 0;

	for (i = 0; i < WATCH_COUNT; i++) {
		if (strcmp(buf, watchlist[i]) != 0)
			continue;

		e = kmalloc_obj(*e, GFP_ATOMIC);
		if (!e)
			return 0;
		e->uid = current_uid().val;
		e->pid = current->pid;
		strscpy(e->path, buf, sizeof(e->path));
		spin_lock(&trip_lock);
		list_add_tail(&e->node, &trip_log);
		spin_unlock(&trip_lock);
		return 0;
	}

	return 0;
}

static struct kprobe kp = {
	.symbol_name = "do_sys_openat2",
	.pre_handler = pre_do_sys_open,
};

static int trip_show(struct seq_file *m, void *v)
{
	struct trip_entry *e;

	spin_lock(&trip_lock);
	list_for_each_entry(e, &trip_log, node)
		seq_printf(m, "[tripwire] uid=%u pid=%d %s\n",
			   e->uid, e->pid, e->path);
	spin_unlock(&trip_lock);
	return 0;
}

static int __init tripwire_init(void)
{
	int err;

	err = register_kprobe(&kp);
	if (err)
		return err;

	proc_entry = proc_create_single("rk_tripwire", 0444, NULL, trip_show);
	if (!proc_entry) {
		unregister_kprobe(&kp);
		return -ENOMEM;
	}

	pr_info("rk: tripwire armed\n");
	return 0;
}

static void __exit tripwire_exit(void)
{
	struct trip_entry *e, *tmp;

	proc_remove(proc_entry);
	unregister_kprobe(&kp);

	list_for_each_entry_safe(e, tmp, &trip_log, node) {
		list_del(&e->node);
		kfree(e);
	}
}

module_init(tripwire_init);
module_exit(tripwire_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 2: file access monitor via do_sys_openat2 hook");
