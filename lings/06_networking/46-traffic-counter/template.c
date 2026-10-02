// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>

static struct proc_dir_entry *entry;

static unsigned int nf_hook_fn(void *priv, struct sk_buff *skb,
			       const struct nf_hook_state *state)
{
	/* TODO: count packets and bytes per direction */
	return NF_ACCEPT;
}

static struct nf_hook_ops nf_ops = {
	.hook     = nf_hook_fn,
	.pf       = NFPROTO_IPV4,
	.hooknum  = NF_INET_PRE_ROUTING,
	.priority = NF_IP_PRI_FIRST,
};

static int counter_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int __init counter_init(void)
{
	int err;

	err = nf_register_net_hook(&init_net, &nf_ops);
	if (err)
		return err;

	entry = proc_create_single("rk_counter", 0444, NULL, counter_show);
	if (!entry) {
		nf_unregister_net_hook(&init_net, &nf_ops);
		return -ENOMEM;
	}

	return 0;
}

static void __exit counter_exit(void)
{
	proc_remove(entry);
	nf_unregister_net_hook(&init_net, &nf_ops);
}

module_init(counter_init);
module_exit(counter_exit);

MODULE_LICENSE("GPL");
