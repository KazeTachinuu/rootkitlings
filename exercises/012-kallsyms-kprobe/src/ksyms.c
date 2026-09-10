#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("Exercice 012: obtenir kallsyms_lookup_name via kprobe");

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
static kallsyms_lookup_name_t my_kallsyms_lookup_name;

static struct kprobe kp = {
    .symbol_name = "kallsyms_lookup_name"
};

static int __init ksyms_init(void)
{
    unsigned long addr;
    int ret;

    printk(KERN_INFO "rootkitlings/012: resolving kallsyms_lookup_name\n");

    // TODO(rootkitlings): Enregistre le kprobe.
    //   ret = register_kprobe(&kp);
    //   Si ret < 0, affiche une erreur et return ret.

    // TODO(rootkitlings): Extrais l'adresse et desenregistre.
    //   my_kallsyms_lookup_name = (kallsyms_lookup_name_t) kp.addr;
    //   unregister_kprobe(&kp);

    printk(KERN_INFO "rootkitlings/012: kallsyms_lookup_name = %px\n",
           my_kallsyms_lookup_name);

    // TODO(rootkitlings): Utilise my_kallsyms_lookup_name pour resoudre
    //   "commit_creds". Stocke le resultat dans addr.
    //   addr = my_kallsyms_lookup_name("commit_creds");

    addr = 0; // remplace cette ligne

    printk(KERN_INFO "rootkitlings/012: commit_creds = 0x%lx\n", addr);

    return 0;
}

static void __exit ksyms_exit(void)
{
    printk(KERN_INFO "rootkitlings/012: unloaded\n");
}

module_init(ksyms_init);
module_exit(ksyms_exit);
