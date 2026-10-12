#ifndef RKLINGS_HOST_PROC_FS_H
#define RKLINGS_HOST_PROC_FS_H

#include <linux/fs.h>

struct proc_dir_entry {
	int unused;
};

struct proc_ops {
	int (*proc_open)(struct inode *, struct file *);
	ssize_t (*proc_read)(struct file *, char *, size_t, loff_t *);
	ssize_t (*proc_write)(struct file *, const char *, size_t, loff_t *);
	loff_t (*proc_lseek)(struct file *, loff_t, int);
	int (*proc_release)(struct inode *, struct file *);
};

static inline void *pde_data(const struct inode *inode)
{
	return inode->data;
}

struct proc_dir_entry *proc_create_data(const char *name, umode_t mode,
					void *parent,
					const struct proc_ops *operations,
					void *data);
void proc_remove(struct proc_dir_entry *entry);

#endif
