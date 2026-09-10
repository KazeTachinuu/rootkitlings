#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define PROC_NAME "rootkitlings"

static struct proc_dir_entry *proc_entry;

static int rk_show(struct seq_file *m, void *v)
{
	seq_printf(m, "rootkitlings/004: proc entry works!\n");
	return 0;
}

static int rk_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_show, NULL);
}

// TODO(rootkitlings): declare une struct proc_ops nommee rk_proc_ops.
//   elle doit contenir:
//     .proc_open    = rk_open,
//     .proc_read    = seq_read,
//     .proc_lseek   = seq_lseek,
//     .proc_release = single_release,


static int __init myproc_init(void)
{
	// TODO(rootkitlings): cree l'entree /proc avec proc_create.
	//   proc_entry = proc_create(PROC_NAME, 0444, NULL, &rk_proc_ops);
	//   si proc_entry est NULL, retourne -ENOMEM.

	printk(KERN_INFO "rootkitlings/004: /proc/%s created\n", PROC_NAME);
	return 0;
}

static void __exit myproc_exit(void)
{
	// TODO(rootkitlings): supprime l'entree /proc.
	//   proc_remove(proc_entry);

	printk(KERN_INFO "rootkitlings/004: /proc/%s removed\n", PROC_NAME);
}

module_init(myproc_init);
module_exit(myproc_exit);
MODULE_LICENSE("GPL");
