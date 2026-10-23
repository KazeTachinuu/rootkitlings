// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>

enum rk_fail_point {
	RK_FAIL_NONE = 0,
	RK_FAIL_FIRST_ALLOC = 1,
	RK_FAIL_SECOND_ALLOC = 2,
};

#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 2
#error "unknown RKLINGS_FAIL_POINT"
#endif

struct rk_channel {
	unsigned int id;
};

static struct rk_channel *chan_a;
static struct rk_channel *chan_b;

static struct rk_channel *rk_channel_create(unsigned int id)
{
	struct rk_channel *ch;

	if ((id == 1 && RKLINGS_FAIL_POINT == RK_FAIL_FIRST_ALLOC) ||
	    (id == 2 && RKLINGS_FAIL_POINT == RK_FAIL_SECOND_ALLOC))
		return ERR_PTR(-ENOMEM);

	ch = kzalloc(sizeof(*ch), GFP_KERNEL);
	if (!ch)
		return ERR_PTR(-ENOMEM);
	ch->id = id;
	return ch;
}

static void rk_channel_destroy(struct rk_channel *ch)
{
	pr_info("rklings: released channel %u\n", ch->id);
	kfree(ch);
}

static int __init rk_init(void)
{
	// TODO 1: allocate both channels; roll back partial initialization
	return 0;
}

static void __exit rk_exit(void)
{
	// TODO 2: release both channels in reverse order
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings failed-load cleanup");
