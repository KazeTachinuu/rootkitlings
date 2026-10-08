// SPDX-License-Identifier: GPL-2.0
#include "persistence.h"
#include <linux/fs.h>
#include <linux/umh.h>

#define REQUEST_SIZE (sizeof(MODULE_REQUEST) - 1)

static int run_helper(const char *program, const char *first,
		      const char *second)
{
	char *argv[] = { (char *)program, (char *)first, (char *)second, NULL };
	char *envp[] = { "PATH=/bin:/sbin:/usr/bin", NULL };

	return call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
}

static int remove_file(void)
{
	return run_helper("/bin/rm", "-f", PERSISTENCE_PATH);
}

enum persistence_state persistence_state(void)
{
	char content[REQUEST_SIZE + 1];
	struct file *file;
	loff_t position = 0;
	ssize_t length;

	file = filp_open(PERSISTENCE_PATH, O_RDONLY, 0);
	if (IS_ERR(file))
		return PTR_ERR(file) == -ENOENT ? PERSISTENCE_ABSENT :
						 PERSISTENCE_FOREIGN;
	length = kernel_read(file, content, sizeof(content), &position);
	filp_close(file, NULL);
	if (length < 0)
		return PERSISTENCE_FOREIGN;
	/* TODO: recognize only the exact MODULE_REQUEST as installed. */
	return PERSISTENCE_FOREIGN;
}

int persistence_install(void)
{
	struct file *file;
	loff_t position = 0;
	ssize_t written;
	int error;

	error = run_helper("/bin/mkdir", "-p", "/etc/modules-load.d");
	if (error)
		return error;
	file = filp_open(PERSISTENCE_PATH,
			 O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (IS_ERR(file))
		return PTR_ERR(file);
	written = kernel_write(file, MODULE_REQUEST, REQUEST_SIZE, &position);
	filp_close(file, NULL);
	if (written == REQUEST_SIZE)
		return 0;
	error = written < 0 ? written : -EIO;
	remove_file();
	return error;
}

int persistence_remove(void)
{
	if (persistence_state() != PERSISTENCE_INSTALLED)
		return -EPERM;
	return remove_file();
}
