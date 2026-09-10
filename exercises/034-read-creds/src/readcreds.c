#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cred.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("034: read process credentials from kernel");

static int __init readcreds_init(void)
{
    pr_info("rootkitlings/034: reading current process credentials\n");

    // TODO(rootkitlings): obtiens les credentials du processus courant
    //   et affiche uid, euid, gid, egid.
    //
    //   1. const struct cred *cred = current_cred();
    //   2. pr_info avec cred->uid.val, cred->euid.val,
    //      cred->gid.val, cred->egid.val
    //
    //   ~4 lignes.

    pr_info("rootkitlings/034: current process: %s (pid %d)\n",
            current->comm, current->pid);

    return 0;
}

static void __exit readcreds_exit(void)
{
    pr_info("rootkitlings/034: unloading\n");
}

module_init(readcreds_init);
module_exit(readcreds_exit);
