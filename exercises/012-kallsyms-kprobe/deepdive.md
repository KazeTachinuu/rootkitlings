# Deepdive, exercice 012

## Pourquoi kallsyms_lookup_name a ete retire

Le commit 0bd476e6c671 ("kallsyms: unexport kallsyms_lookup_name()") a retire
l'export en 2020. La raison: des modules out-of-tree (comme les rootkits)
l'utilisaient pour acceder a des symboles internes non exportes, contournant
l'API stable du kernel.

Questions:
1. Lis le message du commit sur git.kernel.org. Quel etait l'argument principal?
2. Le trick kprobe qu'on vient d'utiliser est-il garanti de fonctionner
   sur les futurs kernels? Que se passe-t-il si kprobes ne peut plus
   resoudre les noms de symboles?
3. Cherche d'autres methodes pour obtenir des adresses de symboles:
   - lire /proc/kallsyms depuis un helper userspace (call_usermodehelper)
   - scanner la memoire kernel a la recherche de patterns connus
   Quels sont les avantages/inconvenients de chaque methode?

## Lien avec la suite

Tu as maintenant un moyen de trouver l'adresse de n'importe quelle fonction
kernel. A l'exercice 013, tu vas explorer struct task_struct, la structure
qui represente un processus dans le kernel.
