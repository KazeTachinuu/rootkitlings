#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cred.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rootkitlings");
MODULE_DESCRIPTION("Exercice 014: lire les credentials du processus courant");

static int __init creds_init(void)
{
    const struct cred *cred;

    printk(KERN_INFO "rootkitlings/014: reading credentials\n");

    // TODO(rootkitlings): Obtiens les credentials du processus courant.
    //   Utilise current_cred() pour obtenir un pointeur const vers struct cred.

    cred = NULL; // remplace cette ligne

    // TODO(rootkitlings): Affiche l'UID reel.
    //   Utilise __kuid_val(cred->uid) pour extraire la valeur numerique.
    //   Format: "rootkitlings/014: uid = %u\n"

    // TODO(rootkitlings): Affiche le GID reel.
    //   Utilise __kgid_val(cred->gid) pour extraire la valeur numerique.
    //   Format: "rootkitlings/014: gid = %u\n"

    // TODO(rootkitlings): Affiche l'EUID (effective UID).
    //   Format: "rootkitlings/014: euid = %u\n"

    return 0;
}

static void __exit creds_exit(void)
{
    printk(KERN_INFO "rootkitlings/014: unloaded\n");
}

module_init(creds_init);
module_exit(creds_exit);
