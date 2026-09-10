# Deepdive: 010 kthread

1. Ton kthread est visible dans `ps aux` entre crochets: [rootkitlings].
   Un defenseur peut voir tous les kthreads. A l'exo 031, on apprendra
   a cacher un processus de /proc. Est-ce que cacher le PID du kthread
   le cacherait aussi de ps ?

2. kthread_stop est bloquant: si ton thread est en ssleep(60), l'exit
   attend 60 secondes. Pour un arret rapide, remplace ssleep par une
   boucle qui verifie kthread_should_stop plus souvent:
   ```c
   for (i = 0; i < 10 && !kthread_should_stop(); i++)
       msleep(100);
   ```
   Cela permet un arret en 100ms max au lieu de 1s.

3. Le kthread qu'on vient de creer va devenir la boucle C2 du rootkit
   (exos 042-047). Il se connectera a un serveur, enverra un beacon,
   recevra des commandes, les executera. Commence a reflechir a la
   structure: connect, loop { recv, exec, send }, disconnect.
