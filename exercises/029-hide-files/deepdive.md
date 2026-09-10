# Deepdive 029

Tu sais maintenant cacher des fichiers par prefixe. C'est la technique
utilisee par la plupart des rootkits reels (Diamorphine, KoviD, etc).

Questions:

1. Le hook agit sur getdents64. Est-ce que `find` utilise aussi getdents64?
   Et `stat`? Teste dans la VM: est-ce que `stat /tmp/rootkit_secret`
   fonctionne meme quand le fichier est cache de ls?

2. Le rootkit KoviD (github.com/carloslack/KoviD) hooke aussi filldir
   (la callback interne utilisee par le VFS). Quelle est la difference
   entre hooker getdents64 (le syscall) et hooker filldir (le callback)?
   Lequel est plus robuste?

3. Notre hook copie le buffer entier en kernel space (kzalloc + copy_from_user),
   le modifie, puis le recopie (copy_to_user). Sur un repertoire avec
   des milliers de fichiers, quel serait le cout en memoire? Comment
   pourrait-on optimiser?

L'exercice 030 etend cette technique pour cacher un dossier specifique.
