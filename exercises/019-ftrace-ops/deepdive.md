# Deepdive, exercice 019

## notrace et la recursion

Le mot-cle `notrace` empeche ftrace d'instrumenter la callback elle-meme.
Sans ca, si la callback appelle une fonction hookee, on entre en recursion
infinie et le kernel crashe.

Questions:
1. Que se passe-t-il si tu retires `notrace`? (ne le fais pas en vrai,
   ou alors avec --snapshot dans la VM)
2. Le flag FTRACE_OPS_FL_RECURSION autorise la recursion controlee.
   Cherche dans kernel/trace/ftrace.c comment il gere le compteur
   de recursion.
3. FTRACE_OPS_FL_IPMODIFY est necessaire pour la suite (exercice 020).
   Il permet de modifier le registre IP dans la callback. Sans lui,
   tu ne peux pas rediriger l'execution vers ta propre fonction.

## Lien avec la suite

Cette callback fait juste un printk. A l'exercice 020, tu vas modifier
le registre IP pour rediriger l'execution vers ta propre version de la
fonction. C'est le vrai hook: l'appelant croit appeler do_sys_openat2,
mais c'est ta fonction qui s'execute.
