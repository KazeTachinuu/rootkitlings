// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/cred.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define PREFIX "/tmp/.rk-"
#define PREFIX_LEN (sizeof(PREFIX) - 1)
#define TRIGGER_PATH_SIZE 128

static atomic_t cmd_count = ATOMIC_INIT(0);

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

static void dispatch(const char *cmd)
{
	if (strcmp(cmd, "ping") == 0) {
		pr_info("pong\n");
	} else if (strcmp(cmd, "id") == 0) {
		pr_info("uid=%u\n",
			from_kuid(&init_user_ns, current_uid()));
	} else if (strcmp(cmd, "count") == 0) {
		pr_info("count=%d\n", atomic_inc_return(&cmd_count));
	}
}

static int pre_rmdir(struct kprobe *p, struct pt_regs *regs)
{
	const char __user *upath = NULL;
	char kpath[TRIGGER_PATH_SIZE];
	long len;

	/* Decode the syscall registers and copy the userspace path without faulting. */
	if (!upath)
		return 0;

	len = copy_user_string_nofault(kpath, upath, sizeof(kpath));
	if (len <= 0 || len >= (long)sizeof(kpath))
		return 0;

	if (strncmp(kpath, PREFIX, PREFIX_LEN) == 0)
		dispatch(kpath + PREFIX_LEN);

	return 0;
}

static struct kprobe kp = {
	.symbol_name = "__x64_sys_rmdir",
	.pre_handler = pre_rmdir,
};

static int __init rmdir_trigger_init(void)
{
	int err;

	err = register_kprobe(&kp);
	if (err)
		return err;

	pr_info("rk: rmdir trigger installed\n");
	return 0;
}

static void __exit rmdir_trigger_exit(void)
{
	unregister_kprobe(&kp);
	pr_info("rk: rmdir trigger removed\n");
}

module_init(rmdir_trigger_init);
module_exit(rmdir_trigger_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rmdir-based command trigger");
