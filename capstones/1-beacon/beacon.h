// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_BEACON_H
#define ROOTKITLINGS_BEACON_H

struct beacon_snapshot {
	unsigned long uptime;
	unsigned long ticks;
	int interval;
};

int beacon_start(void);
void beacon_stop(void);
void beacon_snapshot(struct beacon_snapshot *snapshot);

#endif
