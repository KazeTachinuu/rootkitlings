// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/siphash.h>

static unsigned long bucket;
module_param(bucket, ulong, 0400);
MODULE_PARM_DESC(bucket, "Time bucket used to derive the domain");

static const siphash_key_t campaign_key = {
	.key = {0x6b69746b6f6f7274ULL, 0x73676e696c746f6fULL},
};

static struct proc_dir_entry *dga_entry;

#define DOMAIN_BUFFER_SIZE sizeof("0000000000000000.example")

static void derive_domain(char *domain, size_t size, u64 bucket_value)
{
	u64 label = siphash_1u64(bucket_value, &campaign_key);

	scnprintf(domain, size, "%016llx.example", (unsigned long long)label);
}

static int dga_show(struct seq_file *m, void *unused)
{
	char domain[DOMAIN_BUFFER_SIZE];

	derive_domain(domain, sizeof(domain), bucket);

	seq_printf(m, "bucket=%lu\n", bucket);
	seq_printf(m, "domain=%s\n", domain);
	return 0;
}

static int __init dga_init(void)
{
	dga_entry = proc_create_single("rk_dga", 0400, NULL, dga_show);
	return dga_entry ? 0 : -ENOMEM;
}

static void __exit dga_exit(void)
{
	proc_remove(dga_entry);
}

module_init(dga_init);
module_exit(dga_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Deterministic keyed domain generation");
