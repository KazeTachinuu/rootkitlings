# Deepdive 036

Tu as donne root a un processus distant. C'est puissant mais brut.

Questions:

1. Notre approche a un memory leak: on ne fait pas `put_cred` sur
   l'ancien `struct cred`. En production, ca finirait par consommer
   toute la memoire kernel. Comment corriger ca proprement?
   (Indice: `get_cred` / `put_cred` dans `include/linux/cred.h`)

2. Modifier `task->cred` directement est un data race: si le processus
   est en train de faire un syscall, il peut lire les anciens creds au
   milieu d'une verification de permission. Comment le kernel gere-t-il
   normalement la mise a jour des creds? (Indice: RCU.)

3. Au lieu de modifier les creds d'un processus existant, les vrais
   rootkits preferent executer un shell root via call_usermodehelper.
   Pourquoi est-ce plus propre? (Indice: le processus demarre avec les
   bonnes creds des le debut, pas de race condition.)

L'exercice 037 combine le hooking de kill() avec l'escalation: un signal
magic comme trigger.
