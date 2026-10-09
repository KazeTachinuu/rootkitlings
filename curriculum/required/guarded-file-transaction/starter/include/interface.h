/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_INTERFACE_H
#define ROOTKITLINGS_INTERFACE_H

struct rk_transaction;

int rk_proc_init(struct rk_transaction *transaction);
void rk_proc_exit(void);

#endif
