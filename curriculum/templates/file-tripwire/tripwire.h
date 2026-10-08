/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_TRIPWIRE_H
#define ROOTKITLINGS_TRIPWIRE_H

#include <linux/seq_file.h>

int tripwire_monitor_start(void);
void tripwire_monitor_stop(void);
int tripwire_events_show(struct seq_file *m, void *unused);

#endif
