// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/string_helpers.h>
#include <linux/uidgid.h>
#include <linux/user_namespace.h>

#include "rootkit.h"

static struct proc_dir_entry *proc_entry;

static void rk_print_quoted_comm(struct seq_file *output, const char *comm)
{
	seq_putc(output, '"');
	seq_escape_mem(output, comm, strnlen(comm, TASK_COMM_LEN),
		       ESCAPE_NAP | ESCAPE_APPEND | ESCAPE_HEX, "\\\"");
	seq_putc(output, '"');
}

static int rk_status_show(struct seq_file *output, void *unused)
{
	const struct rk_state *state = output->private;
	struct task_struct *reader = current;
	const struct cred *credentials = current_cred();
	struct user_namespace *display_ns = seq_user_ns(output);
	char reader_comm[TASK_COMM_LEN];
	pid_t reader_tid;
	bool active;

	reader_tid = task_pid_nr(reader);
	get_task_comm(reader_comm, reader);
	active = rk_state_is_active(state);

	seq_printf(output, "active=%u\n", active ? 1U : 0U);
	seq_printf(output, "reader_tid=%d\n", reader_tid);
	seq_puts(output, "reader_comm=");
	rk_print_quoted_comm(output, reader_comm);
	seq_putc(output, '\n');
	seq_printf(output, "uids=%u %u %u %u\n",
		   from_kuid_munged(display_ns, credentials->uid),
		   from_kuid_munged(display_ns, credentials->euid),
		   from_kuid_munged(display_ns, credentials->suid),
		   from_kuid_munged(display_ns, credentials->fsuid));
	seq_printf(output, "gids=%u %u %u %u\n",
		   from_kgid_munged(display_ns, credentials->gid),
		   from_kgid_munged(display_ns, credentials->egid),
		   from_kgid_munged(display_ns, credentials->sgid),
		   from_kgid_munged(display_ns, credentials->fsgid));
	seq_puts(output, "cap_userns=");
	seq_puts(output, credentials->user_ns == &init_user_ns ?
			 "initial\n" : "nested\n");
	seq_printf(output, "cap_permitted=%016llx\n",
		   (unsigned long long)credentials->cap_permitted.val);
	seq_printf(output, "cap_effective=%016llx\n",
		   (unsigned long long)credentials->cap_effective.val);
	return 0;
}

__must_check int rk_proc_publish(struct rk_state *state)
{
	if (proc_entry || !state)
		return -EINVAL;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH)
		proc_entry = NULL;
	else
		proc_entry = proc_create_single_data("rootkitlings", 0444, NULL,
						     rk_status_show, state);
	if (!proc_entry)
		return -ENOMEM;

	return 0;
}

void rk_proc_unpublish(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;
}
