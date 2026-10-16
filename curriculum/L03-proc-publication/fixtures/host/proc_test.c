#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rootkit.h"

static struct proc_dir_entry entry = { .identity = 42 };
static struct proc_dir_entry *creation_result;
static struct proc_dir_entry *removed_entry;
static const char *created_name;
static umode_t created_mode;
static struct proc_dir_entry *created_parent;
static rk_test_show_fn created_show;
static unsigned int create_calls;
static unsigned int remove_calls;

static void require(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "[-] %s\n", message);
		exit(EXIT_FAILURE);
	}
}

struct proc_dir_entry *proc_create_single(const char *name, umode_t mode,
					  struct proc_dir_entry *parent,
					  rk_test_show_fn show)
{
	create_calls++;
	created_name = name;
	created_mode = mode;
	created_parent = parent;
	created_show = show;
	return creation_result;
}

void proc_remove(struct proc_dir_entry *published_entry)
{
	remove_calls++;
	removed_entry = published_entry;
}

int seq_puts(struct seq_file *output, const char *text)
{
	snprintf(output->text, sizeof(output->text), "%s", text);
	return 0;
}

int main(void)
{
	struct seq_file output = { 0 };
	int error;

	if (RKLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH) {
		creation_result = &entry;
		error = rk_proc_publish();
		require(error == -ENOMEM,
			"injected publication failure must return -ENOMEM");
		require(create_calls == 0,
			"injected publication failure must not publish first");
		puts("[+] procfs publication behavior verified");
		return EXIT_SUCCESS;
	}

	creation_result = NULL;
	error = rk_proc_publish();
	require(error == -ENOMEM && create_calls == 1,
		"procfs creation failure must return -ENOMEM");

	creation_result = &entry;
	error = rk_proc_publish();
	require(error == 0 && create_calls == 2,
		"successful publication must call proc_create_single once");
	require(strcmp(created_name, "rklings") == 0 &&
		created_mode == 0444 && created_parent == NULL && created_show,
		"publication must use the required name, mode, root, and show callback");
	require(created_show(&output, NULL) == 0 &&
		strcmp(output.text, "active\n") == 0,
		"the published show callback must produce active followed by newline");

	error = rk_proc_publish();
	require(error == -EINVAL && create_calls == 2,
		"duplicate publication must be rejected without creating another entry");

	rk_proc_unpublish();
	require(remove_calls == 1 && removed_entry == &entry,
		"unpublish must remove the exact published entry");

	error = rk_proc_publish();
	require(error == 0 && create_calls == 3,
		"publication must work again after unpublish clears ownership");
	rk_proc_unpublish();

	puts("[+] procfs publication behavior verified");
	return EXIT_SUCCESS;
}
