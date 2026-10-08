// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/icmp.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/tcp.h>
#include <net/ip.h>

#define MAGIC_ID 0x1337

static struct proc_dir_entry *entry;
static atomic_t trigger_count = ATOMIC_INIT(0);

static unsigned int nf_intercept(void *priv, struct sk_buff *skb,
				 const struct nf_hook_state *state)
{
	struct iphdr ip_buffer;
	struct iphdr *iph;
	struct icmphdr icmp_buffer;
	struct icmphdr *icmph;
	unsigned int network_offset;
	unsigned int icmp_offset;
	bool matched = false;

	if (!skb)
		return NF_ACCEPT;

	network_offset = skb_network_offset(skb);
	iph = skb_header_pointer(skb, network_offset, sizeof(ip_buffer),
				 &ip_buffer);
	if (!iph || iph->version != 4 || iph->ihl < 5 ||
	    iph->protocol != IPPROTO_ICMP || ip_is_fragment(iph) ||
	    ntohs(iph->tot_len) < iph->ihl * 4 + sizeof(icmp_buffer))
		return NF_ACCEPT;

	icmp_offset = network_offset + iph->ihl * 4;
	icmph = skb_header_pointer(skb, icmp_offset, sizeof(icmp_buffer),
				   &icmp_buffer);
	if (!icmph)
		return NF_ACCEPT;
	/* Set matched for an echo request carrying MAGIC_ID. */
	if (matched) {
		atomic_inc(&trigger_count);
		pr_info("rk: magic ICMP trigger received (id=0x%x)\n",
			MAGIC_ID);
	}

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
	seq_printf(m, "triggers=%d\nmagic=0x%x\n", atomic_read(&trigger_count),
		   MAGIC_ID);
	return 0;
}

static int __init nfhook_init(void)
{
	int err;

	err = nf_register_net_hook(&init_net, &nf_ops);
	if (err)
		return err;

	entry = proc_create_single("rk_trigger", 0444, NULL, nf_proc_show);
	if (!entry) {
		nf_unregister_net_hook(&init_net, &nf_ops);
		return -ENOMEM;
	}

	pr_info("rk: ICMP trigger armed, magic id=0x%x\n", MAGIC_ID);
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
MODULE_DESCRIPTION("Magic ICMP trigger detection");
