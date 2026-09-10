#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/list.h>

struct entry {
	int value;
	struct list_head list;
};

// TODO(rootkitlings): declare la tete de liste.
//   LIST_HEAD(entry_list);


static int __init lists_init(void)
{
	struct entry *e;
	int i;
	int values[] = {10, 20, 30};

	printk(KERN_INFO "rootkitlings/008: creating list\n");

	for (i = 0; i < 3; i++) {
		// TODO(rootkitlings): pour chaque valeur:
		//   1. alloue une struct entry avec kmalloc
		//   2. assigne values[i] a e->value
		//   3. ajoute a la liste avec list_add_tail(&e->list, &entry_list)

	}

	// TODO(rootkitlings): itere sur la liste et affiche chaque valeur.
	//   list_for_each_entry(e, &entry_list, list) {
	//       printk(KERN_INFO "rootkitlings/008: value=%d\n", e->value);
	//   }

	return 0;
}

static void __exit lists_exit(void)
{
	struct entry *e, *tmp;

	// TODO(rootkitlings): libere chaque entry de la liste.
	//   utilise list_for_each_entry_safe (pas list_for_each_entry!)
	//   car on modifie la liste pendant l'iteration.
	//   list_for_each_entry_safe(e, tmp, &entry_list, list) {
	//       list_del(&e->list);
	//       kfree(e);
	//   }

	printk(KERN_INFO "rootkitlings/008: list freed\n");
}

module_init(lists_init);
module_exit(lists_exit);
MODULE_LICENSE("GPL");
