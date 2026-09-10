# Hints: 008 linked-lists

## Niveau 1

LIST_HEAD(nom) declare et initialise une tete de liste. C'est un
list_head dont next et prev pointent vers lui-meme (liste vide).

## Niveau 2

Pour creer et ajouter un element:
```c
e = kmalloc(sizeof(*e), GFP_KERNEL);
e->value = values[i];
list_add_tail(&e->list, &entry_list);
```

Pour iterer:
```c
list_for_each_entry(e, &entry_list, list) {
    // e pointe vers chaque struct entry
}
```

## Niveau 3

```c
LIST_HEAD(entry_list);

// dans init:
for (i = 0; i < 3; i++) {
    e = kmalloc(sizeof(*e), GFP_KERNEL);
    if (!e)
        return -ENOMEM;
    e->value = values[i];
    list_add_tail(&e->list, &entry_list);
}

list_for_each_entry(e, &entry_list, list) {
    printk(KERN_INFO "rootkitlings/008: value=%d\n", e->value);
}

// dans exit:
list_for_each_entry_safe(e, tmp, &entry_list, list) {
    list_del(&e->list);
    kfree(e);
}
```
