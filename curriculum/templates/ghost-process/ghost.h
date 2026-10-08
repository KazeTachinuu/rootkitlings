/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_GHOST_H
#define ROOTKITLINGS_GHOST_H

int ghost_start(void);
void ghost_stop(void);
int ghost_pid(void);
int dirhide_start(const char *name);
void dirhide_stop(void);

#endif
