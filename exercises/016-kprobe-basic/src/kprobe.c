#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("Exercice 016: kprobe basique sur do_sys_openat2");

// TODO(rootkitlings): Ecris le pre_handler.
//   Signature: static int handler_pre(struct kprobe *p, struct pt_regs *regs)
//   Dedans, affiche un printk avec le nom de la fonction probee (p->symbol_name).
//   Retourne 0.

static struct kprobe kp = {
    .symbol_name = "do_sys_openat2",
    // TODO(rootkitlings): Assigne ton handler au champ .pre_handler
};

static int __init kprobe_init(void)
{
    int ret;

    printk(KERN_INFO "rootkitlings/016: registering kprobe on do_sys_openat2\n");

    // TODO(rootkitlings): Enregistre le kprobe avec register_kprobe(&kp).
    //   Si ret < 0, affiche une erreur et return ret.

    printk(KERN_INFO "rootkitlings/016: kprobe registered at %px\n", kp.addr);
    return 0;
}

static void __exit kprobe_exit(void)
{
    unregister_kprobe(&kp);
    printk(KERN_INFO "rootkitlings/016: kprobe unregistered\n");
}

module_init(kprobe_init);
module_exit(kprobe_exit);
