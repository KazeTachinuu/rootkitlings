#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define PROC_NAME "rootkitlings_rw"
#define BUF_SIZE 256

static struct proc_dir_entry *proc_entry;
static char msg_buf[BUF_SIZE];
static int msg_len;

static int rk_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s", msg_buf);
	return 0;
}

static int rk_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_show, NULL);
}

// TODO(rootkitlings): ecris la fonction rk_write.
//   signature:
//     static ssize_t rk_write(struct file *file,
//                             const char __user *buf,
//                             size_t count, loff_t *ppos)
//
//   1. si count >= BUF_SIZE, met count = BUF_SIZE - 1
//   2. copie buf vers msg_buf avec copy_from_user(msg_buf, buf, count)
//      si le retour est non-zero, retourne -EFAULT
//   3. msg_buf[count] = '\0';
//   4. msg_len = count;
//   5. retourne count;


static const struct proc_ops rk_proc_ops = {
	.proc_open    = rk_open,
	.proc_read    = seq_read,
	.proc_write   = rk_write,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

static int __init procwrite_init(void)
{
	strscpy(msg_buf, "(empty)\n", BUF_SIZE);
	msg_len = 8;

	proc_entry = proc_create(PROC_NAME, 0666, NULL, &rk_proc_ops);
	if (!proc_entry)
		return -ENOMEM;
	printk(KERN_INFO "rootkitlings/006: /proc/%s created\n", PROC_NAME);
	return 0;
}

static void __exit procwrite_exit(void)
{
	proc_remove(proc_entry);
	printk(KERN_INFO "rootkitlings/006: /proc/%s removed\n", PROC_NAME);
}

module_init(procwrite_init);
module_exit(procwrite_exit);
MODULE_LICENSE("GPL");
