// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/udp.h>

#define DNS_PORT    53
#define SPOOF_PORT  5300

static struct proc_dir_entry *proc_entry;
static atomic_t spoof_count = ATOMIC_INIT(0);

static unsigned int dns_hook(void *priv, struct sk_buff *skb,
			     const struct nf_hook_state *state)
{
	struct iphdr *iph;
	struct udphdr *udph;
	unsigned int hdr_len;

	if (!skb)
		return NF_ACCEPT;

	iph = ip_hdr(skb);
	if (!iph || iph->protocol != IPPROTO_UDP)
		return NF_ACCEPT;

	hdr_len = iph->ihl * 4;
	if (skb->len < hdr_len + sizeof(*udph))
		return NF_ACCEPT;

	udph = (struct udphdr *)((u8 *)iph + hdr_len);

	if (ntohs(udph->dest) != DNS_PORT)
		return NF_ACCEPT;

	if (iph->daddr != htonl(INADDR_LOOPBACK))
		return NF_ACCEPT;

	if (skb_ensure_writable(skb, hdr_len + sizeof(*udph)))
		return NF_ACCEPT;

	iph = ip_hdr(skb);
	udph = (struct udphdr *)((u8 *)iph + iph->ihl * 4);

	udph->dest = htons(SPOOF_PORT);
	udph->check = 0;

	atomic_inc(&spoof_count);
	return NF_ACCEPT;
}

static struct nf_hook_ops nf_ops = {
	.hook     = dns_hook,
	.pf       = NFPROTO_IPV4,
	.hooknum  = NF_INET_LOCAL_OUT,
	.priority = NF_IP_PRI_FIRST,
};

static int proc_read(struct seq_file *m, void *v)
{
	seq_printf(m, "spoofed=%d\nredirect_port=%d\n",
		   atomic_read(&spoof_count), SPOOF_PORT);
	return 0;
}


static int __init dns_spoof_init(void)
{
	int err;

	err = nf_register_net_hook(&init_net, &nf_ops);
	if (err)
		return err;

	proc_entry = proc_create_single("rk_spoofer", 0444, NULL, proc_read);
	if (!proc_entry) {
		nf_unregister_net_hook(&init_net, &nf_ops);
		return -ENOMEM;
	}

	pr_info("rk: DNS spoof active, redirecting port %d -> %d\n",
		DNS_PORT, SPOOF_PORT);
	return 0;
}

static void __exit dns_spoof_exit(void)
{
	proc_remove(proc_entry);
	nf_unregister_net_hook(&init_net, &nf_ops);
}

module_init(dns_spoof_init);
module_exit(dns_spoof_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DNS query redirection via netfilter");
