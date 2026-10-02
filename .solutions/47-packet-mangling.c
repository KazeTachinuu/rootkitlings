// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/in.h>
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
#include <net/ip.h>

// Redirect UDP packets from port 7777 to port 9999
#define TRAP_PORT  htons(7777)
#define REDIR_PORT htons(9999)

static struct proc_dir_entry *entry;
static atomic_t redirected = ATOMIC_INIT(0);

static unsigned int redir_hook(void *priv, struct sk_buff *skb,
			       const struct nf_hook_state *state)
{
	struct iphdr *iph;
	struct udphdr *uh;

	if (!skb)
		return NF_ACCEPT;

	iph = ip_hdr(skb);
	if (!iph || iph->protocol != IPPROTO_UDP)
		return NF_ACCEPT;

	uh = (struct udphdr *)((u8 *)iph + iph->ihl * 4);
	if (uh->dest != TRAP_PORT)
		return NF_ACCEPT;

	if (skb_ensure_writable(skb, (u8 *)uh + sizeof(*uh) - skb->data))
		return NF_ACCEPT;

	iph = ip_hdr(skb);
	uh = (struct udphdr *)((u8 *)iph + iph->ihl * 4);
	uh->dest = REDIR_PORT;
	// Zero UDP checksum (valid for IPv4 UDP, means "no checksum")
	uh->check = 0;
	atomic_inc(&redirected);

	return NF_ACCEPT;
}

static struct nf_hook_ops nf_ops = {
	.hook     = redir_hook,
	.pf       = NFPROTO_IPV4,
	.hooknum  = NF_INET_LOCAL_OUT,
	.priority = NF_IP_PRI_FIRST,
};

static int mangle_show(struct seq_file *m, void *v)
{
	seq_printf(m, "trap_port=7777\nredir_port=9999\nredirected=%d\n",
		   atomic_read(&redirected));
	return 0;
}

static int __init mangler_init(void)
{
	int err;

	err = nf_register_net_hook(&init_net, &nf_ops);
	if (err)
		return err;

	entry = proc_create_single("rk_mangler", 0444, NULL, mangle_show);
	if (!entry) {
		nf_unregister_net_hook(&init_net, &nf_ops);
		return -ENOMEM;
	}

	pr_info("rk: MITM redirect active (port 7777 -> 9999)\n");
	return 0;
}

static void __exit mangler_exit(void)
{
	proc_remove(entry);
	nf_unregister_net_hook(&init_net, &nf_ops);
}

module_init(mangler_init);
module_exit(mangler_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("In-flight UDP port redirect");
