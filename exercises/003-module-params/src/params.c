#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>

static int myint = 0;
static char *mystr = "world";

// TODO(rootkitlings): declare myint comme parametre module de type int.
//   utilise module_param(myint, int, 0444);
//   0444 = lecture seule dans /sys

// TODO(rootkitlings): declare mystr comme parametre module de type charp.
//   utilise module_param(mystr, charp, 0444);
//   charp = char pointer (chaine de caracteres)

// TODO(rootkitlings): ajoute MODULE_PARM_DESC pour chaque parametre.
//   MODULE_PARM_DESC(myint, "un entier de test");
//   MODULE_PARM_DESC(mystr, "une chaine de test");

static int __init params_init(void)
{
	printk(KERN_INFO "rootkitlings/003: myint=%d mystr=%s\n", myint, mystr);
	return 0;
}

static void __exit params_exit(void)
{
	printk(KERN_INFO "rootkitlings/003: unloaded\n");
}

module_init(params_init);
module_exit(params_exit);
MODULE_LICENSE("GPL");
