/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_PERSISTENCE_H
#define ROOTKITLINGS_PERSISTENCE_H

#define PERSISTENCE_PATH "/etc/modules-load.d/hello.conf"
#define MODULE_REQUEST   "hello\n"

enum persistence_state {
	PERSISTENCE_ABSENT,
	PERSISTENCE_INSTALLED,
	PERSISTENCE_FOREIGN,
};

int persistence_install(void);
int persistence_remove(void);
enum persistence_state persistence_state(void);
#endif
