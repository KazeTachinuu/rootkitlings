# Deepdive, exercice 020

## Le probleme de la recursion

Le pattern within_module est simple mais pas parfait. Si un autre module
appelle la fonction hookee, le hook s'active. Mais si TON module appelle
la fonction hookee via un chemin indirect (ex: printk -> vfs_write ->
do_sys_openat2), le check within_module ne le bloquera pas car parent_ip
sera dans le kernel, pas dans ton module.

Questions:
1. Comment Diamorphine gere-t-il ce probleme? Cherche dans le code source
   sur GitHub (m0nad/Diamorphine).
2. Sur les kernels recents (6.x), struct ftrace_regs est differente de
   struct pt_regs. Cherche les changements dans include/linux/ftrace.h.
   Comment cela affecte-t-il le hooking?
3. Est-ce qu'un administrateur peut detecter un hook ftrace?
   Regarde: `cat /sys/kernel/tracing/enabled_functions`
   Ton hook apparait-il dans cette liste?

## Alternative: modifier la stack

Au lieu de changer regs->ip, certains rootkits modifient l'adresse de retour
sur la stack. Cherche "ftrace stack pivoting" pour cette technique avancee.

## Lien avec la suite

L'exercice 021 introduit ftrace_helper.h, un header qui encapsule TOUT ce
pattern (callback, anti-recursion, setup, cleanup) dans des macros simples.
C'est ce qu'on utilisera pour tous les hooks a partir de l'exercice 022.
