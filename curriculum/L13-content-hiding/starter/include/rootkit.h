// SPDX-License-Identifier: GPL-2.0
#ifndef RKLINGS_ROOTKIT_H
#define RKLINGS_ROOTKIT_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

#define RK_FAIL_NONE 0
#define RK_FAIL_PREFLIGHT 1
#define RK_FAIL_PROC_PUBLISH 2
#define RK_FAIL_KRETPROBE_TARGET 3
#define RK_FAIL_COPY_FROM_USER 4
#define RK_FAIL_COPY_TO_USER 5

#define RK_READ_RESULT_LIMIT 512U
#define RK_READ_RETURN_CAPACITY 32U
#define RK_READ_TARGET "/tmp/rklings-read-target"

__must_check int rk_read_target_retain(void);
void rk_read_target_release(void);
__must_check int rk_read_filter_start(void);
void rk_read_filter_stop(void);
unsigned long rk_read_redactions(void);
unsigned long rk_read_oversize_results(void);
unsigned long rk_read_copy_failures(void);
unsigned int rk_read_return_capacity(void);
unsigned int rk_read_returns_missed(void);

__must_check int rk_proc_publish(void);
void rk_proc_unpublish(void);

#endif
