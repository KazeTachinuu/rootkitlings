// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>

#define COMMAND_SIZE 16

static DEFINE_MUTEX(marker_lock);
static struct kobject *marker_kobject;
static struct proc_dir_entry *control_entry;

static ssize_t status_show(struct kobject *kobject,
			   struct kobj_attribute *attribute, char *buffer)
{
	return sysfs_emit(buffer, "visible\n");
}

static struct kobj_attribute status_attribute = __ATTR_RO(status);

static int marker_create_locked(void)
{
	int error;

	marker_kobject = kobject_create_and_add("rk_marker", kernel_kobj);
	if (!marker_kobject)
		return -ENOMEM;
	error = sysfs_create_file(marker_kobject, &status_attribute.attr);
	if (error) {
		kobject_put(marker_kobject);
		marker_kobject = NULL;
	}
	return error;
}

static void marker_destroy_locked(void)
{
	/* TODO: remove the attribute, release the kobject, and clear the pointer. */
}

static int control_show(struct seq_file *output, void *unused)
{
	mutex_lock(&marker_lock);
	seq_printf(output, "visible=%s\n", marker_kobject ? "yes" : "no");
	mutex_unlock(&marker_lock);
	return 0;
}

static int control_open(struct inode *inode, struct file *file)
{
	return single_open(file, control_show, NULL);
}

static ssize_t control_write(struct file *file, const char __user *buffer,
			     size_t count, loff_t *position)
{
	char command[COMMAND_SIZE];
	int error = 0;

	if (!count)
		return -EINVAL;
	if (count >= sizeof(command))
		return -E2BIG;
	if (copy_from_user(command, buffer, count))
		return -EFAULT;
	if (memchr(command, '\0', count))
		return -EINVAL;
	command[count] = '\0';
	strim(command);

	mutex_lock(&marker_lock);
	if (sysfs_streq(command, "hide")) {
		if (!marker_kobject)
			error = -EALREADY;
		else
			marker_destroy_locked();
	} else if (sysfs_streq(command, "show")) {
		if (marker_kobject)
			error = -EALREADY;
		else
			error = marker_create_locked();
	} else {
		error = -EINVAL;
	}
	mutex_unlock(&marker_lock);

	return error ? error : count;
}

static const struct proc_ops control_ops = {
	.proc_open = control_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = control_write,
};

static int __init artifact_init(void)
{
	int error;

	mutex_lock(&marker_lock);
	error = marker_create_locked();
	mutex_unlock(&marker_lock);
	if (error)
		return error;

	control_entry = proc_create("rk_sysfs", 0600, NULL, &control_ops);
	if (control_entry)
		return 0;

	mutex_lock(&marker_lock);
	marker_destroy_locked();
	mutex_unlock(&marker_lock);
	return -ENOMEM;
}

static void __exit artifact_exit(void)
{
	proc_remove(control_entry);
	mutex_lock(&marker_lock);
	marker_destroy_locked();
	mutex_unlock(&marker_lock);
}

module_init(artifact_init);
module_exit(artifact_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Reversible sysfs artifact lifecycle");
