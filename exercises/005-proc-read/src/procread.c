#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>

#define PROC_NAME "rootkitlings_read"

static struct proc_dir_entry *proc_entry;

// TODO(rootkitlings): ecris la fonction rk_show.
//   signature: static int rk_show(struct seq_file *m, void *v)
//   utilise seq_printf pour afficher:
//     "rootkitlings/005: reader=%s pid=%d\n"
//   ou reader = current->comm et pid = current->pid
//   retourne 0


static int rk_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_show, NULL);
}

static const struct proc_ops rk_proc_ops = {
	.proc_open    = rk_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

static int __init procread_init(void)
{
	proc_entry = proc_create(PROC_NAME, 0444, NULL, &rk_proc_ops);
	if (!proc_entry)
		return -ENOMEM;
	printk(KERN_INFO "rootkitlings/005: /proc/%s created\n", PROC_NAME);
	return 0;
}

static void __exit procread_exit(void)
{
	proc_remove(proc_entry);
	printk(KERN_INFO "rootkitlings/005: /proc/%s removed\n", PROC_NAME);
}

module_init(procread_init);
module_exit(procread_exit);
MODULE_LICENSE("GPL");
