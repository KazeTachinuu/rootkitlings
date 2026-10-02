// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/tcp.h>

static struct proc_dir_entry *entry;
static atomic_t tcp_count = ATOMIC_INIT(0);
static atomic_t udp_count = ATOMIC_INIT(0);

static unsigned int sniff_hook(void *priv, struct sk_buff *skb,
			       const struct nf_hook_state *state)
{
	struct iphdr *iph;

	if (!skb)
		return NF_ACCEPT;

	iph = ip_hdr(skb);
	if (!iph)
		return NF_ACCEPT;

	if (iph->protocol == IPPROTO_TCP)
		atomic_inc(&tcp_count);
	else if (iph->protocol == IPPROTO_UDP)
		atomic_inc(&udp_count);

	return NF_ACCEPT;
}

static struct nf_hook_ops nf_ops = {
	.hook     = sniff_hook,
	.pf       = NFPROTO_IPV4,
	.hooknum  = NF_INET_PRE_ROUTING,
	.priority = NF_IP_PRI_FIRST,
};

static int sniff_show(struct seq_file *m, void *v)
{
	seq_printf(m, "tcp=%d\nudp=%d\n",
		   atomic_read(&tcp_count), atomic_read(&udp_count));
	return 0;
}

static int __init sniffer_init(void)
{
	int err;

	err = nf_register_net_hook(&init_net, &nf_ops);
	if (err)
		return err;

	entry = proc_create_single("rk_sniffer", 0444, NULL, sniff_show);
	if (!entry) {
		nf_unregister_net_hook(&init_net, &nf_ops);
		return -ENOMEM;
	}

	pr_info("rk: packet sniffer active, counting TCP and UDP\n");
	return 0;
}

static void __exit sniffer_exit(void)
{
	pr_info("rk: sniffer captured tcp=%d udp=%d\n",
		atomic_read(&tcp_count), atomic_read(&udp_count));
	proc_remove(entry);
	nf_unregister_net_hook(&init_net, &nf_ops);
}

module_init(sniffer_init);
module_exit(sniffer_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Network packet capture");
