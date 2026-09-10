# Deepdive, exercice 024

## Le buffer getdents64 en detail

Le buffer est une zone memoire contigue remplie par le kernel. Chaque
entree a une taille variable (d_reclen) qui inclut le header + le nom + padding.
C'est pour ca qu'on ne peut pas faire `d++` pour avancer: il faut utiliser
d_reclen.

Questions:
1. Pourquoi d_reclen n'est pas simplement sizeof(struct linux_dirent64) +
   strlen(d_name)? (hint: alignement memoire, le kernel padde les entrees)
2. Le champ d_type indique le type de fichier (DT_REG, DT_DIR, DT_LNK...).
   Ou est-il defini? Cherche dans include/linux/fs_types.h.
3. Un repertoire avec 10 000 fichiers fait plusieurs appels a getdents64
   (le buffer a une taille limitee). Ton hook est appele pour CHAQUE appel.
   Comment un rootkit s'assure-t-il de cacher un fichier meme si il est
   dans le deuxieme ou troisieme appel?

## De la lecture a la modification

Pour cacher un fichier (exercice 029), tu vas:
1. Trouver l'entree a cacher dans le buffer
2. Deplacer les entrees suivantes pour ecraser l'entree ciblee (memmove)
3. Reduire nread de la taille de l'entree supprimee
4. Copier le buffer modifie vers l'espace utilisateur (copy_to_user)

## Lien avec la suite

L'exercice 025 couvre le cleanup propre des hooks. Apres ca, on entre
dans la Phase 4: Hiding, ou tu utiliseras ce que tu as appris ici pour
vraiment cacher des fichiers.
