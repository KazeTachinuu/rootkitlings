#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>

static struct task_struct *thread;
static int counter;

// TODO(rootkitlings): ecris la fonction du thread.
//   signature: static int thread_fn(void *data)
//   dans une boucle while (!kthread_should_stop()):
//     1. incremente counter
//     2. printk(KERN_INFO "rootkitlings/010: tick %d\n", counter);
//     3. ssleep(1);  (dort 1 seconde)
//   retourne 0


static int __init kthread_init(void)
{
	counter = 0;

	// TODO(rootkitlings): lance le kthread.
	//   thread = kthread_run(thread_fn, NULL, "rootkitlings");
	//   si IS_ERR(thread), retourne PTR_ERR(thread).

	printk(KERN_INFO "rootkitlings/010: kthread started\n");
	return 0;
}

static void __exit kthread_exit(void)
{
	// TODO(rootkitlings): arrete le kthread.
	//   kthread_stop(thread);

	printk(KERN_INFO "rootkitlings/010: kthread stopped after %d ticks\n",
	       counter);
}

module_init(kthread_init);
module_exit(kthread_exit);
MODULE_LICENSE("GPL");
