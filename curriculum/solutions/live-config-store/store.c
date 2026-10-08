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
	int error = 0;

	if (!*key || !*value)
		return -EINVAL;
	if (strnlen(key, CONFIG_KEY_SIZE) == CONFIG_KEY_SIZE)
		return -ENAMETOOLONG;
	if (strnlen(value, CONFIG_VALUE_SIZE) == CONFIG_VALUE_SIZE)
		return -E2BIG;

	mutex_lock(&config_lock);
	item = config_store_find(key);
	if (!item) {
		item = kmalloc_obj(*item, GFP_KERNEL);
		if (!item) {
			error = -ENOMEM;
			goto unlock;
		}
		strscpy(item->key, key, sizeof(item->key));
		list_add_tail(&item->node, &config_items);
	}
	strscpy(item->value, value, sizeof(item->value));
unlock:
	mutex_unlock(&config_lock);
	return error;
}

void config_store_delete(const char *key)
{
	struct config_item *item;

	mutex_lock(&config_lock);
	item = config_store_find(key);
	if (item) {
		list_del(&item->node);
		kfree(item);
	}
	mutex_unlock(&config_lock);
}

void config_store_show(struct seq_file *m)
{
	struct config_item *item;

	mutex_lock(&config_lock);
	list_for_each_entry(item, &config_items, node) {
		seq_printf(m, "%s=%s\n", item->key, item->value);
	}
	mutex_unlock(&config_lock);
}

void config_store_clear(void)
{
	struct config_item *item, *next;

	mutex_lock(&config_lock);
	list_for_each_entry_safe(item, next, &config_items, node) {
		list_del(&item->node);
		kfree(item);
	}
	mutex_unlock(&config_lock);
}
