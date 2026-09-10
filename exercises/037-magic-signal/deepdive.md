# Deepdive 037

Tu as un trigger invisible pour devenir root. C'est la technique de
Diamorphine (signal 31/63/64), Reptile, et de nombreux rootkits reels.

Questions:

1. Signal 64 est detectable: un auditeur peut monitorer les syscalls
   kill() avec auditd ou strace. Comment rendre le trigger plus discret?
   Pistes: utiliser un paquet reseau specifique, un pattern d'acces
   fichier, ou un ioctl sur un device.

2. Diamorphine utilise plusieurs signaux magic:
   - kill -31: toggle visibilite du module
   - kill -63: toggle visibilite d'un processus
   - kill -64: donner root
   Lis son code source et comprends l'architecture du dispatching.

3. Quel est l'avantage de hooker kill() par rapport a creer une entree
   /proc (exercice 035)? (Indice: /proc/rootkitlings_privesc est visible
   pour tout le monde. kill -64 ne laisse aucune trace dans le filesystem.)

4. Combine les exercices 026-028 (hide module) avec 037 (magic signal).
   Le rootkit se cache automatiquement au chargement, et un signal magic
   donne root. C'est la base d'un rootkit fonctionnel.

Fin de la Phase 5. Tu maitrises l'escalation de privileges. La Phase 6
(Persistence) commence a l'exercice 038.
