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

#define MAGIC_ID 0x1337

static struct proc_dir_entry *entry;
static atomic_t trigger_count = ATOMIC_INIT(0);

static unsigned int icmp_trigger_hook(void *priv, struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	struct iphdr *iph;
	struct icmphdr *icmph;

	if (!skb)
		return NF_ACCEPT;

	iph = ip_hdr(skb);
	if (!iph || iph->protocol != IPPROTO_ICMP)
		return NF_ACCEPT;

	icmph = (struct icmphdr *)((u8 *)iph + (iph->ihl * 4));
	if (icmph->type == ICMP_ECHO &&
	    ntohs(icmph->un.echo.id) == MAGIC_ID) {
		atomic_inc(&trigger_count);
		pr_info("rk: magic ICMP trigger received (id=0x%x)\n",
			MAGIC_ID);
	}

	return NF_ACCEPT;
}

static struct nf_hook_ops nf_ops = {
	.hook     = icmp_trigger_hook,
	.pf       = NFPROTO_IPV4,
	.hooknum  = NF_INET_PRE_ROUTING,
	.priority = NF_IP_PRI_FIRST,
};

static ssize_t trigger_read(struct file *file, char __user *buffer,
			   size_t count, loff_t *position)
{
	char buf[64];
	int len;

	len = snprintf(buf, sizeof(buf), "triggers=%d\nmagic=0x%x\n",
		       atomic_read(&trigger_count), MAGIC_ID);
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops trigger_proc_ops = {
	.proc_read = trigger_read,
};

static int __init icmp_trigger_init(void)
{
	int err;

	err = nf_register_net_hook(&init_net, &nf_ops);
	if (err)
		return err;

	entry = proc_create("rk_trigger", 0444, NULL, &trigger_proc_ops);
	if (!entry) {
		nf_unregister_net_hook(&init_net, &nf_ops);
		return -ENOMEM;
	}

	pr_info("rk: ICMP trigger armed, magic id=0x%x\n", MAGIC_ID);
	return 0;
}

static void __exit icmp_trigger_exit(void)
{
	proc_remove(entry);
	nf_unregister_net_hook(&init_net, &nf_ops);
}

module_init(icmp_trigger_init);
module_exit(icmp_trigger_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Magic ICMP trigger detection");
