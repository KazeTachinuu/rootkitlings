// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

#include <linux/pgtable.h>

static struct proc_dir_entry *proc_entry;

static unsigned long marker __ro_after_init __aligned(PAGE_SIZE) = 0xDEADBEEF;
static unsigned long after_value;

static pte_t *resolve_pte(unsigned long addr)
{
	unsigned long cr3_val;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	asm volatile("mov %%cr3, %0" : "=r"(cr3_val));
	pgd = (pgd_t *)__va(cr3_val & PAGE_MASK) + pgd_index(addr);
	if (pgd_none(*pgd))
		return NULL;

	p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (pud_none(*pud) || pud_leaf(*pud))
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd) || pmd_leaf(*pmd))
		return NULL;

	return pte_offset_kernel(pmd, addr);
}

static void flush_page(unsigned long addr)
{
	asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static int patch_rodata(void)
{
	unsigned long addr = (unsigned long)&marker;
	pte_t *ptep, orig_pte;

	ptep = resolve_pte(addr);
	if (!ptep) {
		pr_err("rk: failed to resolve PTE for %lx\n", addr);
		return -EFAULT;
	}

	orig_pte = *ptep;

	set_pte(ptep, pte_mkwrite_novma(*ptep));
	flush_page(addr);

	WRITE_ONCE(*(unsigned long *)addr, 0xCAFEBABE);

	set_pte(ptep, orig_pte);
	flush_page(addr);

	after_value = READ_ONCE(marker);
	return 0;
}

static ssize_t wp_show(struct file *file, char __user *buffer,
		       size_t count, loff_t *position)
{
	char buf[128];
	int len;

	len = snprintf(buf, sizeof(buf),
		       "before=0x%lX\nafter=0x%lX\nmodified=%s\n",
		       0xDEADBEEFUL, after_value,
		       (after_value == 0xCAFEBABEUL) ? "yes" : "no");
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops wp_ops = {
	.proc_read = wp_show,
};

static int __init wp_bypass_init(void)
{
	int err;

	pr_info("rk: marker before=0x%lX\n", READ_ONCE(marker));

	err = patch_rodata();
	if (err)
		return err;

	pr_info("rk: marker after=0x%lX (wrote to rodata via PTE manipulation)\n",
		after_value);

	proc_entry = proc_create("rk_wrprot", 0444, NULL, &wp_ops);
	if (!proc_entry)
		return -ENOMEM;

	return 0;
}

static void __exit wp_bypass_exit(void)
{
	proc_remove(proc_entry);
}

module_init(wp_bypass_init);
module_exit(wp_bypass_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Write-protection bypass via page table manipulation");
