// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/umh.h>

#define TMP_PATH "/tmp/.rk_out"

struct config_entry {
	char key[32];
	char value[64];
	struct list_head node;
};

static LIST_HEAD(config);
static struct proc_dir_entry *proc_entry;

static void remove_output_file(void)
{
	char *argv[] = { "/bin/sh", "-c", "rm -f " TMP_PATH, NULL };
	char *envp[] = { "PATH=/usr/bin:/bin:/sbin", NULL };
	int ret;

	ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (ret)
		pr_warn("rk: failed to remove command output: %d\n", ret);
}

static struct config_entry *find_entry(const char *key)
{
	struct config_entry *e;

	list_for_each_entry(e, &config, node)
		if (strcmp(e->key, key) == 0)
			return e;
	return NULL;
}

static void set_entry(const char *key, const char *value)
{
	struct config_entry *e;

	e = find_entry(key);
	if (e) {
		strscpy(e->value, value, sizeof(e->value));
		return;
	}

	e = kmalloc_obj(*e, GFP_KERNEL);
	if (!e)
		return;
	strscpy(e->key, key, sizeof(e->key));
	strscpy(e->value, value, sizeof(e->value));
	list_add_tail(&e->node, &config);
}

static void del_entry(const char *key)
{
	struct config_entry *e;

	e = find_entry(key);
	if (e) {
		list_del(&e->node);
		kfree(e);
	}
}

static void exec_cmd(const char *cmd)
{
	char cmdline[256];
	char *argv[] = { "/bin/sh", "-c", cmdline, NULL };
	char *envp[] = { "PATH=/usr/bin:/bin:/sbin", NULL };
	struct file *f;
	char buf[64];
	loff_t pos = 0;
	int len;

	snprintf(cmdline, sizeof(cmdline), "%s > " TMP_PATH " 2>&1", cmd);
	if (call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC))
		return;

	f = filp_open(TMP_PATH, O_RDONLY, 0);
	if (IS_ERR(f))
		return;

	len = kernel_read(f, buf, sizeof(buf) - 1, &pos);
	filp_close(f, NULL);
	remove_output_file();
	if (len <= 0)
		return;

	if (buf[len - 1] == '\n')
		len--;
	buf[len] = '\0';
	set_entry("_result", buf);
}

static int cfg_show(struct seq_file *m, void *v)
{
	struct config_entry *e;

	list_for_each_entry(e, &config, node)
		seq_printf(m, "%s=%s\n", e->key, e->value);
	return 0;
}

static int cfg_open(struct inode *inode, struct file *file)
{
	return single_open(file, cfg_show, NULL);
}

static ssize_t cfg_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *pos)
{
	char line[128];
	size_t len;

	len = count < sizeof(line) - 1 ? count : sizeof(line) - 1;
	if (copy_from_user(line, buf, len))
		return -EFAULT;
	if (len > 0 && line[len - 1] == '\n')
		len--;
	line[len] = '\0';

	if (strncmp(line, "set:", 4) == 0) {
		char *eq = strchr(line + 4, '=');

		if (eq) {
			*eq = '\0';
			set_entry(line + 4, eq + 1);
		}
	} else if (strncmp(line, "del:", 4) == 0) {
		del_entry(line + 4);
	} else if (strncmp(line, "exec:", 5) == 0) {
		exec_cmd(line + 5);
	}

	return count;
}

static const struct proc_ops cfg_proc_ops = {
	.proc_open    = cfg_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = cfg_write,
};

static void purge_config(void)
{
	struct config_entry *e, *tmp;

	list_for_each_entry_safe(e, tmp, &config, node) {
		list_del(&e->node);
		kfree(e);
	}
}

static int __init cfgstore_init(void)
{
	proc_entry = proc_create("rk_cfg", 0600, NULL, &cfg_proc_ops);
	if (!proc_entry)
		return -ENOMEM;

	pr_info("rk: config store ready\n");
	return 0;
}

static void __exit cfgstore_exit(void)
{
	purge_config();
	proc_remove(proc_entry);
	remove_output_file();
}

module_init(cfgstore_init);
module_exit(cfgstore_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 0: kernel config store with command execution");
