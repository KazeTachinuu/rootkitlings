// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *entry;
static atomic64_t bytes_in = ATOMIC64_INIT(0);
static atomic64_t bytes_out = ATOMIC64_INIT(0);
static atomic_t pkts_in = ATOMIC_INIT(0);
static atomic_t pkts_out = ATOMIC_INIT(0);

static unsigned int hook_in(void *priv, struct sk_buff *skb,
			    const struct nf_hook_state *state)
{
	if (skb) {
		atomic_inc(&pkts_in);
		atomic64_add(skb->len, &bytes_in);
	}
	return NF_ACCEPT;
}

static unsigned int hook_out(void *priv, struct sk_buff *skb,
			     const struct nf_hook_state *state)
{
	if (skb) {
		atomic_inc(&pkts_out);
		atomic64_add(skb->len, &bytes_out);
	}
	return NF_ACCEPT;
}

static struct nf_hook_ops nf_ops[] = {
	{
		.hook     = hook_in,
		.pf       = NFPROTO_IPV4,
		.hooknum  = NF_INET_PRE_ROUTING,
		.priority = NF_IP_PRI_FIRST,
	},
	{
		.hook     = hook_out,
		.pf       = NFPROTO_IPV4,
		.hooknum  = NF_INET_POST_ROUTING,
		.priority = NF_IP_PRI_FIRST,
	},
};

static ssize_t counter_read(struct file *file, char __user *buffer,
			   size_t count, loff_t *position)
{
	char buf[128];
	int len;

	len = snprintf(buf, sizeof(buf),
		       "rx_packets=%d\nrx_bytes=%lld\n"
		       "tx_packets=%d\ntx_bytes=%lld\n",
		       atomic_read(&pkts_in), atomic64_read(&bytes_in),
		       atomic_read(&pkts_out), atomic64_read(&bytes_out));
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops counter_proc_ops = {
	.proc_read = counter_read,
};

static int __init counter_init(void)
{
	int err;

	err = nf_register_net_hooks(&init_net, nf_ops, ARRAY_SIZE(nf_ops));
	if (err)
		return err;

	entry = proc_create("rk_counter", 0444, NULL, &counter_proc_ops);
	if (!entry) {
		nf_unregister_net_hooks(&init_net, nf_ops, ARRAY_SIZE(nf_ops));
		return -ENOMEM;
	}

	pr_info("rk: traffic counter active (PRE_ROUTING + POST_ROUTING)\n");
	return 0;
}

static void __exit counter_exit(void)
{
	proc_remove(entry);
	nf_unregister_net_hooks(&init_net, nf_ops, ARRAY_SIZE(nf_ops));
}

module_init(counter_init);
module_exit(counter_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bidirectional traffic counter");
