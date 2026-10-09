/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_AGENT_STATUS_H
#define ROOTKITLINGS_AGENT_STATUS_H

#include <linux/compiler_attributes.h>

struct rk_agent;
struct rk_transaction;

#define RK_FAIL_AGENT_WORKER 12
#define RK_FAIL_AGENT_STATUS_PUBLISH 13

__must_check int rk_agent_status_publish(struct rk_agent *agent,
					 struct rk_transaction *transaction);
void rk_agent_status_unpublish(void);

#endif
