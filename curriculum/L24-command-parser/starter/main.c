// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#define RK_ARG_MAX	128

/* ------------------------------------------------------------------ */
/* Command kinds                                                       */
/* ------------------------------------------------------------------ */

// Define enum rk_cmd_kind and struct rk_command.
// The enum names each command type. The struct holds a parsed kind + argument.

/* ------------------------------------------------------------------ */
/* Parsing helpers                                                     */
/* ------------------------------------------------------------------ */

// Parsing helpers: copy an argument, strip trailing whitespace,
// check prefix match, check exact match.

/* ------------------------------------------------------------------ */
/* Command parser                                                      */
/* ------------------------------------------------------------------ */

// Parse a text buffer into a struct rk_command.
// Some commands carry an argument (e.g. "EXEC ls"), others are bare keywords.

/* ------------------------------------------------------------------ */
/* Command dispatch                                                    */
/* ------------------------------------------------------------------ */

// Dispatch a parsed command to the matching handler.
// Stub handlers for this lesson (return 0).

/* ------------------------------------------------------------------ */
/* Module lifecycle                                                    */
/* ------------------------------------------------------------------ */

static int __init rk_init(void)
{
	return 0;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
