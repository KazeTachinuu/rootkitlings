# Deepdive: 008 linked-lists

1. La liste des modules kernel est exactement ce pattern. Ouvre
   `include/linux/module.h` et regarde le champ `list` dans
   `struct module`. C'est ce list_head qu'on va manipuler a l'exo 026
   pour cacher le module de lsmod. La meme technique, appliquee a une
   liste du kernel au lieu de ta propre liste.

2. Pourquoi list_for_each_entry_SAFE pour liberer ? Parce que list_del
   modifie les pointeurs du noeud courant. Si tu utilises la version
   non-safe, le "next" est corrompu apres list_del et l'iteration crash.
   La version safe sauvegarde le next AVANT d'entrer dans le corps de
   la boucle.

3. `container_of` est le coeur du systeme. Ecris sur papier comment
   elle fonctionne: si tu connais l'adresse d'un membre et l'offset
   de ce membre dans la structure, tu peux calculer l'adresse de la
   structure. C'est `(type *)((char *)ptr - offsetof(type, member))`.
