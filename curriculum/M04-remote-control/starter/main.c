// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/uio.h>
#include <linux/workqueue.h>
#include <net/net_namespace.h>

#define RK_CMD_MAX	256
#define RK_ARG_MAX	128
#define RK_CHUNK_SIZE	4096
#define RK_TIMEOUT_SEC	60
#define RK_FRAME_MAX	4096

/* ------------------------------------------------------------------ */
/* Module parameters                                                   */
/* ------------------------------------------------------------------ */

static unsigned short peer_port;
module_param(peer_port, ushort, 0);

static char peer_addr_str[16] = "127.0.0.1";
module_param_string(peer_address, peer_addr_str, sizeof(peer_addr_str), 0);

/* ------------------------------------------------------------------ */
/* Command protocol (from L24)                                         */
/* ------------------------------------------------------------------ */

/*
 * TODO: Copy the enum rk_cmd_kind, struct rk_command, and the full
 * rk_command_parse implementation from your L24 solution.
 */

/* ------------------------------------------------------------------ */
/* Socket helpers                                                      */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_connect, rk_send_all, rk_recv_all,
 * rk_send_frame, and rk_recv_frame.
 *
 * rk_connect: create a TCP socket and connect to peer_addr:peer_port.
 * rk_send_all: send all bytes, handling partial sends.
 * rk_recv_all: receive exactly len bytes, handling partial recvs.
 * rk_send_frame: send a 4-byte big-endian length header + data.
 * rk_recv_frame: receive a frame, allocate buffer, null-terminate.
 *   Reject frames larger than RK_FRAME_MAX.
 */

/* ------------------------------------------------------------------ */
/* Shell with timeout (from L22)                                       */
/* ------------------------------------------------------------------ */

/*
 * TODO: Copy struct rk_shell and the shell implementation from your
 * L22 solution:
 *   - rk_shell_timeout_fn
 *   - rk_shell_init
 *   - rk_install_fd, rk_shell_init_fn, rk_shell_make_file
 *   - rk_interactive_work, rk_exec_work
 *   - rk_start_interactive, rk_start_exec
 *   - rk_shell_stop
 *
 * Declare two static instances: interactive_shell and exec_shell.
 */

/* ------------------------------------------------------------------ */
/* File download (from L23)                                            */
/* ------------------------------------------------------------------ */

/*
 * TODO: Copy rk_download_file from your L23 solution. It opens a file
 * with filp_open, reads in RK_CHUNK_SIZE chunks with kernel_read,
 * sends each chunk as a framed message, and sends a zero-length frame
 * at EOF.
 */

/* ------------------------------------------------------------------ */
/* Privilege escalation                                                */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_escalate.
 *
 * prepare_kernel_cred(&init_task) + commit_creds.
 */

/* ------------------------------------------------------------------ */
/* Command dispatch                                                    */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_send_response(sock, resp) to send a text
 * response as a framed message.
 *
 * TODO: Implement rk_command_dispatch(sock, cmd).
 *
 * Switch on cmd->kind:
 *   PING      -> send "PONG"
 *   STATUS    -> send "OK"
 *   SHELL     -> rk_start_interactive, send "SHELL OK"
 *   EXEC      -> rk_start_exec(cmd->arg), send "EXEC OK"
 *   DOWNLOAD  -> rk_download_file(sock, cmd->arg)
 *   HIDE/UNHIDE variants -> call handler, send "OK"
 *   ESCALATE  -> rk_escalate, send "ESCALATE OK"
 *   UNKNOWN   -> send "ERR unknown command"
 */

/* ------------------------------------------------------------------ */
/* Agent kthread loop                                                  */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_agent_fn(void *data).
 *
 * The agent loop:
 * 1. While !kthread_should_stop:
 *    a. rk_recv_frame to get the next command.
 *    b. rk_command_parse to parse it.
 *    c. kfree the frame buffer.
 *    d. rk_command_dispatch to execute and respond.
 *    e. On socket error, break out of the loop.
 * 2. Return 0.
 */

/* ------------------------------------------------------------------ */
/* Module lifecycle                                                    */
/* ------------------------------------------------------------------ */

static struct task_struct *agent_thread;
static struct socket *agent_sock;

static int __init rk_init(void)
{
	/*
	 * TODO:
	 * 1. Reject load if peer_port is 0.
	 * 2. Initialize both shell instances with rk_shell_init.
	 * 3. Connect to the operator with rk_connect.
	 * 4. Start the agent kthread with kthread_run.
	 * 5. On kthread failure, shut down and release the socket.
	 */
	return -ENOSYS;
}

static void __exit rk_exit(void)
{
	/*
	 * TODO:
	 * 1. Shut down the agent socket to unblock the recv loop.
	 * 2. kthread_stop the agent thread.
	 * 3. Release the agent socket.
	 * 4. Stop both shells with rk_shell_stop.
	 * 5. Cancel any pending work items.
	 */
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
