# Deepdive, exercice 021

## ftrace_helper.h dans la vraie vie

Ce header est devenu un pattern standard. Presque tous les rootkits kernel
modernes en utilisent une variante:

- Diamorphine (m0nad): utilise un pattern similaire, pas ce header exact
- Reptile: version modifiee avec support multi-arch
- KoviD (carloslack): version etendue avec gestion des erreurs avancee

Questions:
1. Lis le code de ftrace_helper.h. Retrouve les concepts des exercices
   019 (ftrace_ops, set_filter_ip, register) et 020 (callback, within_module).
2. Le container_of dans fh_ftrace_thunk: comment fonctionne-t-il?
   (hint: il calcule l'offset de ops dans struct ftrace_hook pour retrouver
   le hook a partir du pointeur ftrace_ops)
3. Que se passe-t-il si tu hooks une fonction qui n'existe pas (typo dans
   le nom)? Suis le code: fh_lookup_name retourne 0, fh_install_hook
   retourne -ENOENT.

## Lien avec la suite

A partir de maintenant, tous les exercices de hooking (022-025) utilisent
ftrace_helper.h. Tu vas hooker mkdir (022), kill (023), getdents64 (024),
et apprendre le cleanup propre (025).
