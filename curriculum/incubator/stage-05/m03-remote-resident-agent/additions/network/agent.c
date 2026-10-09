// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/string.h>

#include "agent.h"

int rk_agent_start(struct rk_agent *agent, u16 peer_port,
		   unsigned int retry_base_ms, unsigned int retry_max_ms)
{
	/* TODO: own one reconnecting worker and one published socket. */
	return -EOPNOTSUPP;
}

void rk_agent_stop(struct rk_agent *agent)
{
	/* TODO: close the stop/publish race, shut down, then join. */
}

void rk_agent_snapshot(struct rk_agent *agent, struct rk_agent_status *status)
{
	if (status)
		memset(status, 0, sizeof(*status));
}
