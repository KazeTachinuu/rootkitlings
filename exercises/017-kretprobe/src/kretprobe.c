#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("Exercice 017: kretprobe sur do_mkdirat");

// TODO(rootkitlings): Ecris le ret_handler.
//   Signature: static int ret_handler(struct kretprobe_instance *ri,
//                                     struct pt_regs *regs)
//   La valeur de retour de la fonction hookee est dans regs->ax.
//   Caste-la en int (c'est un code d'erreur): int retval = regs->ax;
//   Affiche: "rootkitlings/017: do_mkdirat returned %d\n"
//   Retourne 0.

static struct kretprobe kretp = {
    .kp.symbol_name = "do_mkdirat",
    // TODO(rootkitlings): Assigne .handler a ton ret_handler
    .maxactive = 20,
};

static int __init kretprobe_init(void)
{
    int ret;

    printk(KERN_INFO "rootkitlings/017: registering kretprobe on do_mkdirat\n");

    // TODO(rootkitlings): Enregistre le kretprobe avec register_kretprobe(&kretp).
    //   Si ret < 0, affiche une erreur et return ret.

    printk(KERN_INFO "rootkitlings/017: kretprobe registered\n");
    return 0;
}

static void __exit kretprobe_exit(void)
{
    unregister_kretprobe(&kretp);
    printk(KERN_INFO "rootkitlings/017: kretprobe unregistered (missed %d)\n",
           kretp.nmissed);
}

module_init(kretprobe_init);
module_exit(kretprobe_exit);
