#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rootkit.h"

struct rk_state {
	bool active;
	bool actions_enabled;
	bool vfs_mkdir_seen;
	bool filename_rmdir_seen;
};

static const char *probe_names[RK_PROBE_COUNT];
static unsigned int probe_name_calls;

#include <interface/proc.c>

static void fail(const char *message)
{
	fprintf(stderr, "[x] %s\n", message);
	exit(1);
}

int seq_printf(struct seq_file *output, const char *format, ...)
{
	va_list arguments;
	int count;

	output->output_calls++;
	if (output->length >= output->capacity)
		return -1;
	va_start(arguments, format);
	count = vsnprintf(output->buffer + output->length,
			  output->capacity - output->length, format, arguments);
	va_end(arguments);
	if (count < 0 || (size_t)count >= output->capacity - output->length)
		return -1;
	output->length += (size_t)count;
	return count;
}

int seq_open(struct file *file, const struct seq_operations *operations)
{
	(void)file;
	(void)operations;
	return 0;
}

ssize_t seq_read(struct file *file, char *buffer, size_t count,
		 loff_t *position)
{
	(void)file;
	(void)buffer;
	(void)count;
	(void)position;
	return 0;
}

loff_t seq_lseek(struct file *file, loff_t offset, int whence)
{
	(void)file;
	(void)whence;
	return offset;
}

int seq_release(struct inode *inode, struct file *file)
{
	(void)inode;
	(void)file;
	return 0;
}

struct proc_dir_entry *proc_create_data(const char *name, umode_t mode,
					void *parent,
					const struct proc_ops *operations,
					void *data)
{
	static struct proc_dir_entry entry;

	(void)name;
	(void)mode;
	(void)parent;
	(void)operations;
	(void)data;
	return &entry;
}

void proc_remove(struct proc_dir_entry *entry)
{
	(void)entry;
}

unsigned long copy_from_user(void *destination, const void *source,
			     unsigned long count)
{
	memcpy(destination, source, count);
	return 0;
}

bool rk_state_is_active(const struct rk_state *state)
{
	return state->active;
}

bool rk_state_actions_enabled(const struct rk_state *state)
{
	return state->actions_enabled;
}

void rk_state_set_actions_enabled(struct rk_state *state, bool enabled)
{
	state->actions_enabled = enabled;
}

bool rk_state_vfs_mkdir_seen(const struct rk_state *state)
{
	return state->vfs_mkdir_seen;
}

bool rk_state_filename_rmdir_seen(const struct rk_state *state)
{
	return state->filename_rmdir_seen;
}

const char *rk_probe_name(enum rk_probe_id id)
{
	probe_name_calls++;
	if (id >= RK_PROBE_COUNT)
		return NULL;
	return probe_names[id];
}

static void check_view(const char *retained_name, const char *selected_name)
{
	char actual[512] = { 0 };
	char expected[512];
	struct rk_state state = {
		.active = true,
		.actions_enabled = true,
		.vfs_mkdir_seen = true,
		.filename_rmdir_seen = true,
	};
	struct seq_file output = {
		.private = &state,
		.buffer = actual,
		.capacity = sizeof(actual),
	};
	void *item;
	loff_t position = 0;
	unsigned int selected_output_calls;
	size_t selected_length;
	int result;

	probe_names[RK_PROBE_VFS_MKDIR] = retained_name;
	probe_names[RK_PROBE_FILENAME_RMDIR] = selected_name;
	probe_name_calls = 0;
	item = rk_proc_start(&output, &position);
	if (!item)
		fail("iterator omitted the retained catalog item");
	result = rk_proc_show(&output, item);
	if (result != 0)
		fail("renderer suppressed the retained catalog item");
	item = rk_proc_next(&output, item, &position);
	if (!item)
		fail("iterator omitted the selected catalog item");
	selected_output_calls = output.output_calls;
	selected_length = output.length;
	result = rk_proc_show(&output, item);
	if (result != SEQ_SKIP)
		fail("renderer did not suppress the selected catalog item");
	if (output.output_calls != selected_output_calls ||
	    output.length != selected_length)
		fail("renderer emitted output before suppressing the selected item");
	item = rk_proc_next(&output, item, &position);
	if (item)
		fail("iterator produced an item beyond the catalog");
	if (probe_name_calls != 1)
		fail("renderer must obtain exactly the retained name from the catalog");

	if (snprintf(expected, sizeof(expected),
		     "active=1\nactions=enabled\nvfs_mkdir_seen=1\n"
		     "filename_rmdir_seen=1\nlisted_probe=%s\n",
		     retained_name) < 0)
		fail("could not construct expected output");
	if (strcmp(actual, expected) != 0)
		fail("renderer changed the retained catalog record");
}

int main(void)
{
	check_view("vfs_mkdir", "filename_rmdir");
	check_view("renamed_retained_probe", "renamed_selected_probe");
	puts("[+] selected ID suppression and catalog-derived output verified");
	return 0;
}
