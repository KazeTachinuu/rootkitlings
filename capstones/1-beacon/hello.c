// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

static int interval = 5;

static int interval_set(const char *val, const struct kernel_param *kp)
{
	int v, err;

	err = kstrtoint(val, 10, &v);
	if (err)
		return err;
	if (v < 1 || v > 300)
		return -EINVAL;
	interval = v;
	return 0;
}

static const struct kernel_param_ops interval_ops = {
	.set = interval_set,
	.get = param_get_int,
};

module_param_cb(interval, &interval_ops, &interval, 0644);
MODULE_PARM_DESC(interval, "Beacon interval in seconds (1-300)");

static struct task_struct *beacon_thread;
static struct proc_dir_entry *proc_entry;
static DEFINE_MUTEX(state_lock);
static unsigned long start_jiffies;
static unsigned long ticks;

static int beacon_loop(void *data)
{
	while (!kthread_should_stop()) {
		schedule_timeout_interruptible(HZ * interval);
		if (kthread_should_stop())
			break;
		mutex_lock(&state_lock);
		ticks++;
		mutex_unlock(&state_lock);
	}
	return 0;
}

static int beacon_show(struct seq_file *m, void *v)
{
	unsigned long up, t;

	mutex_lock(&state_lock);
	up = (jiffies - start_jiffies) / HZ;
	t = ticks;
	mutex_unlock(&state_lock);

	seq_printf(m, "uptime: %lus\nticks: %lu\ninterval: %d\nstatus: running\n",
		   up, t, interval);
	return 0;
}

static int __init beacon_init(void)
{
	start_jiffies = jiffies;

	proc_entry = proc_create_single("rk_beacon", 0444, NULL, beacon_show);
	if (!proc_entry)
		return -ENOMEM;

	beacon_thread = kthread_run(beacon_loop, NULL, "rk_beacon");
	if (IS_ERR(beacon_thread)) {
		proc_remove(proc_entry);
		return PTR_ERR(beacon_thread);
	}

	pr_info("rk: beacon started, interval=%d\n", interval);
	return 0;
}

static void __exit beacon_exit(void)
{
	kthread_stop(beacon_thread);
	proc_remove(proc_entry);
}

module_init(beacon_init);
module_exit(beacon_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 1: periodic kernel beacon with kthread");
