# Deepdive 041: self-reload

## Question ouverte

call_usermodehelper est l'une des API les plus surveillees par les outils
de securite kernel (LKRG, SELinux, AppArmor). Pourquoi ?

Cherche comment LKRG (Linux Kernel Runtime Guard) detecte les appels
suspects a call_usermodehelper. Quels heuristiques utilise-t-il ?

Autre piste: un rootkit plus sophistique n'utiliserait pas call_usermodehelper
du tout. Il hookrait le handler de rmmod pour empecher le dechargement.
Comment ferais-tu ca avec ce que tu as appris dans les exos de hooking ?

L'exercice 042 commence la phase communication: creer un socket TCP
depuis le kernel.
