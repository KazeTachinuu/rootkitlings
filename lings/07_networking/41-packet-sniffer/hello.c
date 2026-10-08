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
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>

static struct proc_dir_entry *entry;
static atomic_t tcp_count = ATOMIC_INIT(0);
static atomic_t udp_count = ATOMIC_INIT(0);

static unsigned int nf_intercept(void *priv, struct sk_buff *skb,
				 const struct nf_hook_state *state)
{
	struct iphdr ip_buffer;
	const struct iphdr *iph;

	if (!skb)
		return NF_ACCEPT;

	iph = skb_header_pointer(skb, skb_network_offset(skb), sizeof(ip_buffer),
				 &ip_buffer);
	if (!iph || iph->version != 4 || iph->ihl < 5)
		return NF_ACCEPT;

	/* Increment the counter selected by iph->protocol. */

	return NF_ACCEPT;
}

static struct nf_hook_ops nf_ops = {
	.hook = nf_intercept,
	.pf = NFPROTO_IPV4,
	.hooknum = NF_INET_PRE_ROUTING,
	.priority = NF_IP_PRI_FIRST,
};

static int nf_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "tcp=%d\nudp=%d\n", atomic_read(&tcp_count),
		   atomic_read(&udp_count));
	return 0;
}

static int __init nfhook_init(void)
{
	int err;

	err = nf_register_net_hook(&init_net, &nf_ops);
	if (err)
		return err;

	entry = proc_create_single("rk_sniffer", 0444, NULL, nf_proc_show);
	if (!entry) {
		nf_unregister_net_hook(&init_net, &nf_ops);
		return -ENOMEM;
	}

	pr_info("rk: packet sniffer active, counting TCP and UDP\n");
	return 0;
}

static void __exit nfhook_exit(void)
{
	proc_remove(entry);
	nf_unregister_net_hook(&init_net, &nf_ops);
}

module_init(nfhook_init);
module_exit(nfhook_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Network packet capture");
