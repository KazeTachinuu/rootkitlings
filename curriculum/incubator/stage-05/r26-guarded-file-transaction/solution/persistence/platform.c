// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/magic.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/security.h>
#include <linux/uidgid.h>
#include <linux/user_namespace.h>

#include "platform.h"

#define RK_ROOT "/run/rootkitlings-r26"
#define RK_TARGET RK_ROOT "/request.conf"
#define RK_SIBLING RK_ROOT "/.request.conf.rootkitlings"

static int fail_step;
module_param(fail_step, int, 0400);
MODULE_PARM_DESC(fail_step, "Inject one transaction-stage failure");

static unsigned int io_limit;
module_param(io_limit, uint, 0400);
MODULE_PARM_DESC(io_limit, "Limit each read or write result");

static struct path root_path;
static bool prepared;
static bool fail_used;

bool rk_platform_should_fail(enum rk_platform_step step)
{
	if (fail_step != step || fail_used)
		return false;
	fail_used = true;
	return true;
}

static int rk_fill_object(struct file *file, struct rk_object *object)
{
	struct inode *inode = file_inode(file);

	if (file->f_path.mnt != root_path.mnt ||
	    file->f_path.dentry->d_parent != root_path.dentry)
		return -EXDEV;
	object->handle = file;
	object->identity = (u64)(unsigned long)inode;
	object->length = (u64)i_size_read(inode);
	object->mode = inode->i_mode;
	object->links = inode->i_nlink;
	object->uid = from_kuid(&init_user_ns, inode->i_uid);
	object->gid = from_kgid(&init_user_ns, inode->i_gid);
	return 0;
}

static int rk_open(const char *path, int flags, umode_t mode,
		   enum rk_platform_step step, struct rk_object *object)
{
	struct file *file;
	int error;

	if (rk_platform_should_fail(step))
		return -EIO;
	file = filp_open(path, flags, mode);
	if (IS_ERR(file))
		return PTR_ERR(file);
	error = rk_fill_object(file, object);
	if (error)
		fput(file);
	return error;
}

int rk_platform_prepare(void)
{
	struct inode *inode;
	int error;

	fail_used = false;
	error = kern_path(RK_ROOT, LOOKUP_NO_SYMLINKS, &root_path);
	if (error)
		return error;
	inode = d_inode(root_path.dentry);
	if (root_path.dentry != root_path.mnt->mnt_root ||
	    root_path.dentry->d_sb->s_magic != TMPFS_MAGIC ||
	    !S_ISDIR(inode->i_mode) || (inode->i_mode & 07777) != 0700 ||
	    !uid_eq(inode->i_uid, GLOBAL_ROOT_UID) ||
	    !gid_eq(inode->i_gid, GLOBAL_ROOT_GID) ||
	    (root_path.mnt->mnt_flags & (MNT_NOSUID | MNT_NODEV | MNT_NOEXEC)) !=
	    (MNT_NOSUID | MNT_NODEV | MNT_NOEXEC)) {
		path_put(&root_path);
		return -EPERM;
	}
	prepared = true;
	return 0;
}

void rk_platform_finish(void)
{
	if (prepared) {
		path_put(&root_path);
		prepared = false;
	}
}

int rk_platform_open_target(struct rk_object *object, enum rk_platform_step step)
{
	return rk_open(RK_TARGET,
		       O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_LARGEFILE, 0,
		       step, object);
}

int rk_platform_create_candidate(struct rk_object *object)
{
	return rk_open(RK_SIBLING,
		       O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_LARGEFILE,
		       0600, RK_STEP_CREATE_CANDIDATE, object);
}

int rk_platform_open_sibling(struct rk_object *object,
			     enum rk_platform_step step)
{
	return rk_open(RK_SIBLING,
		       O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_LARGEFILE, 0,
		       step, object);
}

void rk_platform_close(struct rk_object *object)
{
	if (object->handle)
		fput(object->handle);
	object->handle = NULL;
}

long rk_platform_read(struct rk_object *object, u8 *buffer, size_t length,
		      size_t *position)
{
	loff_t offset = (loff_t)*position;
	ssize_t result;

	if (rk_platform_should_fail(RK_STEP_READ))
		return -EIO;
	if (io_limit && length > io_limit)
		length = io_limit;
	result = kernel_read(object->handle, buffer, length, &offset);
	if (result > 0)
		*position = (size_t)offset;
	return result;
}

long rk_platform_write(struct rk_object *object, const u8 *buffer,
		       size_t length, size_t *position)
{
	loff_t offset = (loff_t)*position;
	ssize_t result;

	if (rk_platform_should_fail(RK_STEP_WRITE))
		return -EIO;
	if (io_limit && length > io_limit)
		length = io_limit;
	result = kernel_write(object->handle, buffer, length, &offset);
	if (result > 0)
		*position = (size_t)offset;
	return result;
}

int rk_platform_sync_file(struct rk_object *object)
{
	if (rk_platform_should_fail(RK_STEP_SYNC_CANDIDATE))
		return -EIO;
	return vfs_fsync(object->handle, 0);
}

int rk_platform_exchange(struct rk_object *target, struct rk_object *sibling,
			 enum rk_platform_step step)
{
	struct file *target_file = target->handle;
	struct file *sibling_file = sibling->handle;
	struct renamedata rename = {
		.mnt_idmap = mnt_idmap(target_file->f_path.mnt),
		.new_parent = sibling_file->f_path.dentry->d_parent,
		.delegated_inode = NULL,
		.flags = RENAME_EXCHANGE,
	};
	int error;

	if (rk_platform_should_fail(step))
		return -EIO;
	if (target_file->f_path.mnt != sibling_file->f_path.mnt ||
	    target_file->f_path.dentry->d_parent !=
	    sibling_file->f_path.dentry->d_parent)
		return -EXDEV;
	error = mnt_want_write(target_file->f_path.mnt);
	if (error)
		return error;
	error = start_renaming_two_dentries(&rename,
					    target_file->f_path.dentry,
					    sibling_file->f_path.dentry);
	if (!error) {
		struct path old_parent = {
			.mnt = target_file->f_path.mnt,
			.dentry = rename.old_parent,
		};
		struct path new_parent = {
			.mnt = sibling_file->f_path.mnt,
			.dentry = rename.new_parent,
		};

		error = security_path_rename(&old_parent, rename.old_dentry,
					     &new_parent, rename.new_dentry,
					     RENAME_EXCHANGE);
		if (!error)
			error = vfs_rename(&rename);
		end_renaming(&rename);
	}
	mnt_drop_write(target_file->f_path.mnt);
	return error;
}

int rk_platform_sync_directory(enum rk_platform_step step)
{
	struct file *directory;
	int error;

	if (rk_platform_should_fail(step))
		return -EIO;
	directory = filp_open(RK_ROOT, O_RDONLY | O_DIRECTORY | O_NOFOLLOW, 0);
	if (IS_ERR(directory))
		return PTR_ERR(directory);
	if (directory->f_path.mnt != root_path.mnt ||
	    directory->f_path.dentry != root_path.dentry)
		error = -ESTALE;
	else
		error = vfs_fsync(directory, 0);
	fput(directory);
	return error;
}

int rk_platform_unlink_sibling(struct rk_object *expected)
{
	struct file *expected_file = expected->handle;
	struct path path;
	struct dentry *parent;
	struct dentry *child;
	int error;

	if (rk_platform_should_fail(RK_STEP_UNLINK))
		return -EIO;
	error = kern_path(RK_SIBLING, LOOKUP_NO_SYMLINKS, &path);
	if (error)
		return error;
	if (path.mnt != root_path.mnt || path.dentry->d_parent != root_path.dentry) {
		error = -EXDEV;
		goto out_path;
	}
	error = mnt_want_write(path.mnt);
	if (error)
		goto out_path;
	parent = dget_parent(path.dentry);
	child = start_removing_dentry(parent, path.dentry);
	if (IS_ERR(child)) {
		error = PTR_ERR(child);
		goto out_parent;
	}
	if (d_inode(child) != file_inode(expected_file))
		error = -ESTALE;
	else {
		struct path parent_path = {
			.mnt = path.mnt,
			.dentry = parent,
		};

		error = security_path_unlink(&parent_path, child);
		if (!error)
			error = vfs_unlink(mnt_idmap(path.mnt), d_inode(parent),
					   child, NULL);
	}
	end_removing(child);
out_parent:
	dput(parent);
	mnt_drop_write(path.mnt);
out_path:
	path_put(&path);
	return error;
}

void rk_platform_warn_preserved(void)
{
	pr_warn("rootkitlings: transaction incomplete; stopping without further changes\n");
}
