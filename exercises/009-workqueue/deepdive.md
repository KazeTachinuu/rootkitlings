# Deepdive: 009 workqueue

1. Que se passe-t-il si tu oublies cancel_delayed_work_sync dans l'exit ?
   Le work est encore planifie mais le module est decharge. Quand le
   timer expire, le kernel essaie d'appeler une fonction qui n'existe
   plus en memoire. Resultat: kernel panic. Essaie (en mode snapshot
   pour ne pas casser ta VM).

2. La workqueue systeme (system_wq) est partagee par tout le kernel.
   Un rootkit qui fait du travail lourd dans une workqueue pourrait
   affecter les performances du systeme et etre detecte. Regarde
   `create_singlethread_workqueue` pour creer une workqueue dediee.

3. Difference entre workqueue et kthread (exo 010): une workqueue
   execute des taches ponctuelles, un kthread est un thread permanent.
   Pour un beacon C2 qui tourne en continu, un kthread est souvent
   plus adapte. Pour une tache unique (envoyer une alerte), un work
   suffit.
