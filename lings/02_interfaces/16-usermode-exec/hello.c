// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/module.h>

#define MARKER_PATH "/tmp/rk_helper_marker"

static char *helper_env[] __maybe_unused = {
	"HOME=/",
	"PATH=/usr/sbin:/usr/bin:/sbin:/bin",
	NULL,
};

static int run_helper(char **argv)
{
	/* Run argv[0] with this argv and helper_env, then wait for it. */
	return -EOPNOTSUPP;
}

static int __init helper_init(void)
{
	char *argv[] = { "/bin/touch", MARKER_PATH, NULL };

	return run_helper(argv);
}

static void __exit helper_exit(void)
{
	char *argv[] = { "/bin/rm", "-f", MARKER_PATH, NULL };

	run_helper(argv);
}

module_init(helper_init);
module_exit(helper_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Run an explicit userspace helper");
