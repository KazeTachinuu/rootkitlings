#ifndef ROOTKITLINGS_HOST_SEQ_FILE_H
#define ROOTKITLINGS_HOST_SEQ_FILE_H

#include <linux/fs.h>
#include <stddef.h>

#define SEQ_SKIP 1

struct seq_file {
	void *private;
	char *buffer;
	size_t capacity;
	size_t length;
	unsigned int output_calls;
};

struct seq_operations {
	void *(*start)(struct seq_file *, loff_t *);
	void *(*next)(struct seq_file *, void *, loff_t *);
	void (*stop)(struct seq_file *, void *);
	int (*show)(struct seq_file *, void *);
};

int seq_printf(struct seq_file *output, const char *format, ...);
int seq_open(struct file *file, const struct seq_operations *operations);
ssize_t seq_read(struct file *file, char *buffer, size_t count,
		 loff_t *position);
loff_t seq_lseek(struct file *file, loff_t offset, int whence);
int seq_release(struct inode *inode, struct file *file);

#endif
