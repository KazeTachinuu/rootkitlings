# Deepdive: 006 proc-write

1. Tu utilises un buffer statique (tableau global). Quels problemes
   si deux processus ecrivent en meme temps ? Cherche "race condition"
   et "mutex" dans le contexte kernel. On verra la synchronisation
   plus tard, mais reflechis au probleme.

2. Un rootkit utilise souvent /proc comme interface de commande.
   Par exemple, Diamorphine (un rootkit open source connu) utilise
   un signal specifique. D'autres utilisent /proc pour recevoir:
   - "hide PID" pour cacher un processus
   - "root" pour escalader les privileges
   - "connect IP:PORT" pour demarrer le C2
   Comment securiserais-tu cette interface pour que seul l'attaquant
   puisse l'utiliser ? (indice: l'exo password vient plus tard)

3. Que se passe-t-il si tu retournes 0 au lieu de count dans la
   fonction write ? Essaie. Le userspace va re-envoyer les memes
   donnees en boucle (il croit que rien n'a ete ecrit).
