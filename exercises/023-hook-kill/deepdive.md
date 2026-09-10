# Deepdive, exercice 023

## Signaux magiques dans Diamorphine

Clone Diamorphine et lis le code:
```bash
git clone https://github.com/m0nad/Diamorphine.git
```

Questions:
1. Diamorphine utilise les signaux 31, 63, 64. Pourquoi ces numeros?
   (hint: les signaux standard POSIX vont jusqu'a 31, les real-time de
   32 a 64. 63 et 64 sont les derniers real-time, rarement utilises.)
2. Comment un defenseur peut-il detecter qu'un hook existe sur kill?
   (hint: comparer l'adresse dans sys_call_table avec ce qu'elle devrait etre)
3. Alternative au hooking de kill: utiliser /proc ou un fichier special
   pour les commandes. Quels sont les avantages/inconvenients de chaque
   approche?

## Detection

Un outil comme auditd peut logger les appels a kill(). Mais si le rootkit
hooke kill AVANT que auditd ne recoive l'evenement, le signal magique
n'apparait jamais dans les logs.

## Lien avec la suite

L'exercice 024 hooke getdents64, le syscall utilise par ls. C'est la
preparation pour le file hiding (exercice 029).
