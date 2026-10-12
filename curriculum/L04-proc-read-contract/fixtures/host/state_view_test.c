#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry fixture_entry;
static int (*captured_show)(struct seq_file *, void *);
static void *captured_data;

struct proc_dir_entry *proc_create_single_data(
	const char *name, unsigned int mode, struct proc_dir_entry *parent,
	int (*show)(struct seq_file *, void *), void *data)
{
	assert(strcmp(name, "rklings") == 0);
	assert(mode == 0444);
	assert(parent == NULL);
	assert(show != NULL);
	assert(data != NULL);
	captured_show = show;
	captured_data = data;
	return &fixture_entry;
}

void proc_remove(struct proc_dir_entry *entry)
{
	assert(entry == &fixture_entry);
}

int seq_printf(struct seq_file *output, const char *format, ...)
{
	va_list arguments;
	int length;

	va_start(arguments, format);
	length = vsnprintf(output->buffer, output->capacity, format, arguments);
	va_end(arguments);
	assert(length >= 0);
	assert((size_t)length < output->capacity);
	return length;
}

#include "core/state.c"
#include "interface/proc.c"

static void expect_render(struct rk_state *state, bool active,
			  const char *expected)
{
	char rendered[32] = {0};
	struct seq_file output = {
		.private = state,
		.buffer = rendered,
		.capacity = sizeof(rendered),
	};

	state->active = active;
	assert(rk_state_is_active(state) == active);
	assert(captured_show(&output, NULL) == 0);
	assert(strcmp(rendered, expected) == 0);
}

int main(void)
{
	struct rk_state *state = rk_state_create();

	assert(state != NULL);
	assert(rk_proc_publish(state) == 0);
	assert(captured_data == state);
	expect_render(state, true, "active=1\n");
	expect_render(state, false, "active=0\n");
	rk_proc_unpublish();
	state->active = true;
	rk_state_destroy(state);
	puts("[+] proc view follows both live state values");
	return 0;
}
