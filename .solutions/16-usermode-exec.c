// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#define TMP_PATH "/tmp/.rk_out"

static struct proc_dir_entry *proc_entry;
static char output[256];
static int output_len;

static ssize_t exec_read(struct file *file, char __user *buf,
			 size_t count, loff_t *pos)
{
	return simple_read_from_buffer(buf, count, pos,
				       output, output_len);
}

static const struct proc_ops exec_ops = {
	.proc_read = exec_read,
};

static int __init exec_init(void)
{
	char *argv[] = { "/bin/sh", "-c",
			 "uname -r > " TMP_PATH, NULL };
	char *envp[] = { "PATH=/usr/bin:/bin", NULL };
	struct file *f;
	loff_t pos = 0;
	int ret;

	ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (ret) {
		pr_err("rk: usermodehelper failed: %d\n", ret);
		return -EIO;
	}

	f = filp_open(TMP_PATH, O_RDONLY, 0);
	if (IS_ERR(f))
		return PTR_ERR(f);

	output_len = kernel_read(f, output, sizeof(output) - 1, &pos);
	filp_close(f, NULL);

	if (output_len < 0)
		return output_len;
	output[output_len] = '\0';

	{
		char *rm_argv[] = { "/bin/sh", "-c",
				    "rm -f " TMP_PATH, NULL };
		call_usermodehelper(rm_argv[0], rm_argv, envp,
				    UMH_WAIT_PROC);
	}

	proc_entry = proc_create("rk_exec", 0444, NULL, &exec_ops);
	if (!proc_entry)
		return -ENOMEM;

	pr_info("rk: captured uname: %s", output);
	return 0;
}

static void __exit exec_exit(void)
{
	proc_remove(proc_entry);
	pr_info("rk: cleaned up\n");
}

module_init(exec_init);
module_exit(exec_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Usermode command execution");
