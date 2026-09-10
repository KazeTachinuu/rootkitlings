# Deepdive 026

Tu as retire le module de la liste `modules`. Mais tape `ls /sys/module/`
dans la VM: ton module apparait toujours.

Pourquoi? Parce que /sys/module/ est construit via sysfs, pas via la liste
chainee `modules`. Le kernel expose chaque module comme un kobject dans sysfs.

Va lire `kernel/module/sysfs.c` sur Elixir Bootlin et cherche ou le kobject
du module est enregistre. La fonction `module_add_modinfo_attrs` et
`mod_sysfs_setup` sont les points d'entree.

Question: pourquoi le kernel maintient-il deux representations separees
(liste chainee + sysfs) du meme ensemble de modules?

L'exercice 027 regle le probleme sysfs.

Bonus: un defenseur peut-il detecter un module cache? Cherche "Volatility
linux hidden modules" pour voir comment l'analyse memoire reconstruit la
liste en scannant les pool tags.
