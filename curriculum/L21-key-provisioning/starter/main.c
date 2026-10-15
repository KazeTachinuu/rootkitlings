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

/* TODO: Declare control_key as module_param_string, permission 0 */

static struct dentry *rk_hook_dentry;
static int (*rk_orig_mkdir)(struct mnt_idmap *, struct inode *,
			    struct dentry *, umode_t);

/*
 * TODO: Implement rk_handle_mkdir.
 *
 * Parse mkdir names as rootkit commands:
 *
 *   "key.rotate"    : overwrite rk_key with 32 random bytes
 *                     (get_random_bytes). Return -EEXIST to prevent
 *                     directory creation.
 *
 *   "key.<64 hex>"  : parse the 64 hex characters after "key." with
 *                     hex2bin into rk_key. Return -EINVAL on bad hex,
 *                     -EEXIST on success.
 *
 *   Anything else   : pass through to rk_orig_mkdir.
 *
 * All commands start with "key." (strncmp 4 bytes).
 *
 * static int rk_handle_mkdir(struct mnt_idmap *idmap, struct inode *dir,
 *                            struct dentry *dentry, umode_t mode)
 */

static int __init rk_init(void)
{
	/*
	 * TODO:
	 * 1. Reject load if rk_key_hex is empty or strlen != RK_KEY_HEX_LEN.
	 * 2. hex2bin(rk_key, rk_key_hex, RK_KEY_LEN). Reject on failure.
	 * 3. memzero_explicit(rk_key_hex, sizeof(rk_key_hex)).
	 * 4. kern_path("/tmp", LOOKUP_DIRECTORY, &path) to find hook point.
	 * 5. dget(path.dentry), save it to rk_hook_dentry.
	 * 6. Save inode->i_op->mkdir to rk_orig_mkdir.
	 * 7. Replace inode->i_op->mkdir with rk_handle_mkdir.
	 * 8. path_put(&path), return 0.
	 * 9. On failure after hex2bin, zeroize rk_key.
	 */
	return -ENOSYS;
}

static void __exit rk_exit(void)
{
	/*
	 * TODO:
	 * 1. If rk_hook_dentry is set, restore rk_orig_mkdir and dput.
	 * 2. memzero_explicit(rk_key, sizeof(rk_key)).
	 */
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
