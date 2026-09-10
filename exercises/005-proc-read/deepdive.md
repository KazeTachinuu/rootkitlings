# Deepdive: 005 proc-read

1. Tu utilises `current->comm` pour le nom du processus. Ouvre
   `include/linux/sched.h` sur Elixir Bootlin et regarde la definition
   de `struct task_struct`. C'est LA structure centrale du kernel pour
   les processus. Elle fait plus de 700 lignes. Repere les champs:
   pid, comm, cred, fs, files, mm.

2. Quand tu fais `cat /proc/rootkitlings_read`, le `current` dans
   rk_show est le processus `cat`. Si tu fais `python3 -c "open('/proc/rootkitlings_read').read()"`,
   current->comm sera "python3". Un rootkit peut utiliser ca pour
   se comporter differemment selon qui lit le fichier.

3. seq_file alloue un buffer interne qui grandit. Que se passe-t-il si
   tu ecris enormement de donnees avec seq_printf ? Cherche `seq_buf_alloc`
   dans fs/seq_file.c.
