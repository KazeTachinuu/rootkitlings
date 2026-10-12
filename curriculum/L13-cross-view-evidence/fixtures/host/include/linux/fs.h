#ifndef RKLINGS_HOST_FS_H
#define RKLINGS_HOST_FS_H

#include <linux/types.h>

struct inode {
	void *data;
};

struct file {
	void *private_data;
	struct inode *inode;
};

static inline struct inode *file_inode(const struct file *file)
{
	return file->inode;
}

#endif
