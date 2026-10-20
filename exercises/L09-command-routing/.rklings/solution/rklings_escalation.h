/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RKLINGS_ESCALATION_H
#define _RKLINGS_ESCALATION_H

#include <linux/types.h>

int rk_escalate_pid(pid_t pid);
int rk_escalation_count(void);

#endif /* _RKLINGS_ESCALATION_H */
