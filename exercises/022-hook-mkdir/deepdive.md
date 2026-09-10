# Deepdive, exercice 022

## strncpy_from_user et la securite

`strncpy_from_user` fait plus que copier: il verifie que le pointeur
est bien en espace utilisateur (pas un pointeur kernel deguise). Sans
cette verification, un attaquant pourrait exploiter ton hook pour lire
de la memoire kernel arbitraire.

Questions:
1. Que fait `access_ok()` et pourquoi strncpy_from_user l'appelle?
   Cherche dans arch/x86/include/asm/uaccess.h.
2. Si le pathname fait plus de 256 chars, ton buffer est trop petit.
   Le kernel definit PATH_MAX a 4096. Est-ce un probleme en pratique?
3. Un rootkit qui hooke mkdirat a un avantage: il voit TOUTES les
   creations de dossiers. Un outil comme inotifywait ne voit que les
   dossiers qu'il surveille. Compare les deux approches.

## Lien avec la suite

L'exercice 023 hooke kill() pour detecter un signal "magique". C'est
la methode classique pour envoyer des commandes a un rootkit.
