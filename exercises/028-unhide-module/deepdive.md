# Deepdive 028

Tu sais maintenant cacher un module et le restaurer proprement. Mais cette
approche a une faiblesse: entre le insmod et le rmmod, si le kernel panic,
le module est perdu (les pointeurs sauvegardes sont en memoire volatile).

Questions:

1. Est-ce qu'on pourrait sauvegarder les pointeurs dans un fichier? Quels
   problemes ca poserait (hint: le filesystem est dans le VFS, et ecrire
   depuis le kernel est deconseille)?

2. Le rootkit Diamorphine (github.com/m0nad/Diamorphine) utilise un signal
   magic pour toggler la visibilite: envoyer kill -31 cache le module,
   re-envoyer kill -31 le re-montre. Regarde son code source, comment
   implemente-t-il le toggle?

3. Un module charge mais cache peut-il quand meme etre detecte par un
   outil qui scanne /proc/kallsyms? Pourquoi?

On passe maintenant aux choses serieuses: cacher des fichiers du filesystem.
Exercice 029.
