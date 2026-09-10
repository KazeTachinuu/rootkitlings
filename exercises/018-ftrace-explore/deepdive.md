# Deepdive, exercice 018

## Ftrace et la securite

Ftrace est un outil de debug/profiling legitime. Mais un module kernel
peut utiliser l'API ftrace (register_ftrace_function) pour hooker
n'importe quelle fonction de la liste available_filter_functions.

Questions:
1. Y a-t-il un mecanisme pour empecher un module de s'enregistrer
   sur ftrace? Cherche dans le code de register_ftrace_function.
2. LKRG (Linux Kernel Runtime Guard) surveille les hooks ftrace.
   Comment detecte-t-il un hook malveillant? Cherche "ftrace" sur
   le repo LKRG sur GitHub.
3. Sur un kernel avec CONFIG_FTRACE=n, est-ce qu'un rootkit peut
   encore hooker des fonctions? (hint: oui, mais pas via ftrace.
   Il faut patcher le code en memoire directement.)

## Lien avec la suite

A l'exercice 019, tu vas utiliser l'API ftrace depuis un module kernel
pour registrer une callback sur une fonction. C'est le debut du
hooking ftrace, la technique de base de tous les rootkits modernes.
