// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/random.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/string.h>

#define RK_KEY_LEN	32
#define RK_KEY_HEX_LEN	64

static u8 rk_key[RK_KEY_LEN];
static char rk_key_hex[RK_KEY_HEX_LEN + 1];

// Declare control_key as module_param_string, permission 0.

static struct dentry *rk_hook_dentry;
static int (*rk_orig_mkdir)(struct mnt_idmap *, struct inode *,
			    struct dentry *, umode_t);

// Implement rk_handle_mkdir: intercept "key.rotate" and "key.<hex>" commands.

static int __init rk_init(void)
{
	// Parse hex key from module parameter, hook mkdir on /tmp.
	return -ENOSYS;
}

static void __exit rk_exit(void)
{
	// Restore original mkdir, zeroize key material.
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
