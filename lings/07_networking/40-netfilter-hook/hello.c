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

static struct proc_dir_entry *entry;
static atomic_t pkt_count = ATOMIC_INIT(0);

static unsigned int nf_intercept(void *priv, struct sk_buff *skb,
				 const struct nf_hook_state *state)
{
	atomic_inc(&pkt_count);
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
	seq_printf(m, "%d\n", atomic_read(&pkt_count));
	return 0;
}

static int __init nfhook_init(void)
{
	int err;

	/* Register nf_ops in init_net and store the status in err. */
	err = -EINVAL;
	if (err)
		return err;

	entry = proc_create_single("rk_nfhook", 0444, NULL, nf_proc_show);
	if (!entry) {
		nf_unregister_net_hook(&init_net, &nf_ops);
		return -ENOMEM;
	}

	pr_info("rk: netfilter hook registered on PRE_ROUTING\n");
	return 0;
}

static void __exit nfhook_exit(void)
{
	proc_remove(entry);
	/* Unregister nf_ops before this module's hook disappears. */
}

module_init(nfhook_init);
module_exit(nfhook_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Netfilter packet interception");
