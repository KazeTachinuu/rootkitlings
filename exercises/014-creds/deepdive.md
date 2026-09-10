# Deepdive, exercice 014

## Le modele copy-on-write des credentials

Les credentials sont en copy-on-write. Quand un processus fork(), le fils
partage les memes cred que le pere (refcount++). Si l'un veut modifier
ses creds, il doit:
1. prepare_creds() : copie la struct cred
2. modifier la copie
3. commit_creds() : remplace atomiquement l'ancienne par la nouvelle

Questions:
1. Pourquoi ne pas modifier les creds in-place? (hint: RCU, lecteurs
   concurrents sur d'autres CPUs)
2. Ouvre commit_creds() dans kernel/cred.c. Que fait-il avec l'ancien
   pointeur cred? Pourquoi utilise-t-il rcu_assign_pointer()?
3. Les capabilities (CAP_NET_ADMIN, CAP_SYS_ADMIN, etc.) sont aussi dans
   struct cred. Cherche `cap_effective` dans la structure. Quelle est la
   difference entre un process avec uid=0 et un process avec toutes les
   capabilities?

## Lien avec la suite

A l'exercice 035, tu vas utiliser prepare_kernel_cred(NULL) + commit_creds()
pour donner les credentials root a un processus. C'est la base de la
privilege escalation depuis le kernel.
