#ifndef FIXTURE_LINUX_PROC_FS_H
#define FIXTURE_LINUX_PROC_FS_H

struct seq_file;

struct proc_dir_entry {
	unsigned int sentinel;
};

struct proc_dir_entry *proc_create_single_data(
	const char *name, unsigned int mode, struct proc_dir_entry *parent,
	int (*show)(struct seq_file *, void *), void *data);
void proc_remove(struct proc_dir_entry *entry);

#endif
