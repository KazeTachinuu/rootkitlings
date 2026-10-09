// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_ROOTKIT_H
#define ROOTKITLINGS_ROOTKIT_H

#include <linux/compiler_attributes.h>
#include "packet_redirect.h"

#define RK_FAIL_NONE 0
#define RK_FAIL_NETFILTER_REGISTER 1
#define RK_FAIL_PROC_PUBLISH 2

__must_check int rk_proc_publish(struct rk_netfilter_mutator *mutator);
void rk_proc_unpublish(void);

__must_check int rk_run_selftests(void);

#endif
