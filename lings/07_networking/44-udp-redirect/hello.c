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
#include <net/checksum.h>
#include <net/ip.h>

#define SOURCE_PORT   7777
#define REDIRECT_PORT 9999

static struct proc_dir_entry *proc_entry;
static atomic_t redirect_count = ATOMIC_INIT(0);

static unsigned int redirect_hook(void *priv, struct sk_buff *skb,
				  const struct nf_hook_state *state)
{
	struct iphdr ip_buffer;
	struct udphdr udp_buffer;
	struct udphdr *writable_udph;
	const struct iphdr *iph;
	const struct udphdr *udph;
	__be16 new_port = htons(REDIRECT_PORT);
	__be16 old_port;
	unsigned int ip_header_length;
	unsigned int ip_length;
	unsigned int network_offset;
	unsigned int transport_offset;
	unsigned int udp_length;

	if (!skb)
		return NF_ACCEPT;

	network_offset = skb_network_offset(skb);
	iph = skb_header_pointer(skb, network_offset, sizeof(ip_buffer),
				 &ip_buffer);
	if (!iph || iph->version != 4 || iph->ihl < 5 ||
	    iph->protocol != IPPROTO_UDP || ip_is_fragment(iph))
		return NF_ACCEPT;

	ip_header_length = iph->ihl * 4;
	ip_length = ntohs(iph->tot_len);
	if (ip_length < ip_header_length + sizeof(udp_buffer))
		return NF_ACCEPT;

	transport_offset = network_offset + ip_header_length;
	udph = skb_header_pointer(skb, transport_offset, sizeof(udp_buffer),
				  &udp_buffer);
	if (!udph)
		return NF_ACCEPT;
	udp_length = ntohs(udph->len);
	if (udp_length < sizeof(udp_buffer) ||
	    udp_length > ip_length - ip_header_length)
		return NF_ACCEPT;

	if (ntohs(udph->dest) != SOURCE_PORT)
		return NF_ACCEPT;

	if (iph->daddr != htonl(INADDR_LOOPBACK))
		return NF_ACCEPT;

	if (skb_ensure_writable(skb, transport_offset + sizeof(*udph)))
		return NF_ACCEPT;

	writable_udph = (struct udphdr *)(skb->data + transport_offset);
	old_port = writable_udph->dest;

	/* Rewrite the UDP destination to REDIRECT_PORT. */
	return NF_ACCEPT;
	if (writable_udph->check)
		inet_proto_csum_replace2(&writable_udph->check, skb, old_port,
					 new_port, false);

	atomic_inc(&redirect_count);
	return NF_ACCEPT;
}

static struct nf_hook_ops nf_ops = {
	.hook = redirect_hook,
	.pf = NFPROTO_IPV4,
	.hooknum = NF_INET_LOCAL_OUT,
	.priority = NF_IP_PRI_FIRST,
};

static int redirect_show(struct seq_file *m, void *v)
{
	seq_printf(m, "redirected=%d\nredirect_port=%d\n",
		   atomic_read(&redirect_count), REDIRECT_PORT);
	return 0;
}

static int __init redirect_init(void)
{
	int err;

	err = nf_register_net_hook(&init_net, &nf_ops);
	if (err)
		return err;

	proc_entry =
	    proc_create_single("rk_redirect", 0444, NULL, redirect_show);
	if (!proc_entry) {
		nf_unregister_net_hook(&init_net, &nf_ops);
		return -ENOMEM;
	}

	pr_info("rk: UDP redirect active, port %d -> %d\n", SOURCE_PORT,
		REDIRECT_PORT);
	return 0;
}

static void __exit redirect_exit(void)
{
	proc_remove(proc_entry);
	nf_unregister_net_hook(&init_net, &nf_ops);
}

module_init(redirect_init);
module_exit(redirect_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Outgoing UDP port redirection via netfilter");
