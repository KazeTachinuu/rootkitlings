#include <linux/module.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>

static struct delayed_work my_work;
static int counter;

static void work_callback(struct work_struct *work)
{
	counter++;
	printk(KERN_INFO "rootkitlings/009: tick %d\n", counter);

	// TODO(rootkitlings): re-planifie le work pour une nouvelle execution
	//   dans 1 seconde (1000 ms).
	//   schedule_delayed_work(&my_work, msecs_to_jiffies(1000));
}

static int __init workq_init(void)
{
	counter = 0;

	// TODO(rootkitlings): initialise le delayed_work et planifie-le.
	//   INIT_DELAYED_WORK(&my_work, work_callback);
	//   schedule_delayed_work(&my_work, msecs_to_jiffies(1000));

	printk(KERN_INFO "rootkitlings/009: workqueue started\n");
	return 0;
}

static void __exit workq_exit(void)
{
	// TODO(rootkitlings): annule le work en cours.
	//   cancel_delayed_work_sync attend que le callback en cours finisse
	//   avant de retourner, ce qui evite un use-after-free.
	//   cancel_delayed_work_sync(&my_work);

	printk(KERN_INFO "rootkitlings/009: workqueue stopped after %d ticks\n",
	       counter);
}

module_init(workq_init);
module_exit(workq_exit);
MODULE_LICENSE("GPL");
