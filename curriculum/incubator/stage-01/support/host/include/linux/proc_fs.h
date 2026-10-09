#ifndef ROOTKITLINGS_HOST_PROC_FS_H
#define ROOTKITLINGS_HOST_PROC_FS_H

#include <stddef.h>

struct proc_dir_entry {
	int identity;
};
struct seq_file;

typedef unsigned short umode_t;
typedef int (*rk_test_show_fn)(struct seq_file *, void *);

struct proc_dir_entry *proc_create_single(const char *name, umode_t mode,
					  struct proc_dir_entry *parent,
					  rk_test_show_fn show);
void proc_remove(struct proc_dir_entry *entry);

#endif
