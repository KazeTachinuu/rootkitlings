#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/dirent.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "ftrace_helper.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("Exercice 024: hooker getdents64, lire les directory entries");

static asmlinkage long (*orig_getdents64)(const struct pt_regs *);

static asmlinkage long hook_getdents64(const struct pt_regs *regs)
{
    long nread;
    void __user *dirent_buf;
    struct linux_dirent64 *kbuf, *d;
    unsigned long offset;

    /* Appelle l'original d'abord pour obtenir le buffer rempli */
    nread = orig_getdents64(regs);
    if (nread <= 0)
        return nread;

    /* Le buffer userspace est le deuxieme argument (regs->si) */
    dirent_buf = (void __user *)regs->si;

    /* Alloue un buffer kernel pour la copie */
    kbuf = kmalloc(nread, GFP_KERNEL);
    if (!kbuf)
        return nread;

    /* Copie le buffer userspace vers le kernel */
    if (copy_from_user(kbuf, dirent_buf, nread)) {
        kfree(kbuf);
        return nread;
    }

    // TODO(rootkitlings): Itere sur les entrees du buffer et affiche chaque nom.
    //   offset = 0;
    //   while (offset < nread) {
    //       d = (struct linux_dirent64 *)((char *)kbuf + offset);
    //       printk(KERN_INFO "rootkitlings/024: entry: %s\n", d->d_name);
    //       offset += d->d_reclen;
    //   }

    kfree(kbuf);
    return nread;
}

static struct ftrace_hook hooks[] = {
    HOOK("__x64_sys_getdents64", hook_getdents64, &orig_getdents64),
};

static int __init hookdents_init(void)
{
    int ret = fh_install_hooks(hooks, ARRAY_SIZE(hooks));

    if (ret)
        return ret;
    printk(KERN_INFO "rootkitlings/024: getdents64 hook installed\n");
    return 0;
}

static void __exit hookdents_exit(void)
{
    fh_remove_hooks(hooks, ARRAY_SIZE(hooks));
    printk(KERN_INFO "rootkitlings/024: getdents64 hook removed\n");
}

module_init(hookdents_init);
module_exit(hookdents_exit);
