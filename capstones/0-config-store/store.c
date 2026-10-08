// SPDX-License-Identifier: GPL-2.0
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "config.h"

struct config_item {
	char key[CONFIG_KEY_SIZE];
	char value[CONFIG_VALUE_SIZE];
	struct list_head node;
};

static LIST_HEAD(config_items);
static DEFINE_MUTEX(config_lock);

static struct config_item *config_store_find(const char *key)
{
	/* config_lock must be held. */
	struct config_item *item;

	list_for_each_entry(item, &config_items, node) {
		if (!strcmp(item->key, key))
			return item;
	}
	return NULL;
}

int config_store_set(const char *key, const char *value)
{
	struct config_item *item;

	mutex_lock(&config_lock);
	item = config_store_find(key);
	if (item) {
		strscpy(item->value, value, sizeof(item->value));
		mutex_unlock(&config_lock);
		return 0;
	}
	mutex_unlock(&config_lock);
	/* Validate and append a new item. */
	return -EOPNOTSUPP;
}

void config_store_delete(const char *key)
{
	/* Lock, then unlink and free the matching item if it exists. */
}

void config_store_show(struct seq_file *m)
{
	/* Hold config_lock while emitting one line for every item. */
}

void config_store_clear(void)
{
	/* Lock, then safely unlink and free every item. */
}
