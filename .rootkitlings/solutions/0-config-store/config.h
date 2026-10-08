/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_CONFIG_H
#define ROOTKITLINGS_CONFIG_H

#include <linux/seq_file.h>

#define CONFIG_KEY_SIZE	  32
#define CONFIG_VALUE_SIZE 64

int config_store_set(const char *key, const char *value);
void config_store_delete(const char *key);
void config_store_show(struct seq_file *m);
void config_store_clear(void);

#endif
