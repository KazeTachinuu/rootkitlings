# Deepdive, exercice 017

## Le trampoline de retour

Kretprobes modifie la stack pour remplacer l'adresse de retour par un
trampoline. Ca veut dire que si le kernel crashe pendant que la fonction
hookee s'execute, le stack trace sera faux (il montrera le trampoline
au lieu du vrai appelant).

Questions:
1. Que se passe-t-il si maxactive est trop petit sur une fonction tres
   appelee? Regarde kretp.nmissed apres un test charge.
2. Peut-on MODIFIER la valeur de retour dans le ret_handler?
   (hint: oui, en ecrivant dans regs->ax. C'est utile pour un rootkit
   qui veut faire croire qu'une operation a echoue)
3. L'entry_handler + data_size permettent de stocker des donnees entre
   l'entree et le retour. Comment l'utiliser pour mesurer le temps
   d'execution d'une fonction? (hint: ktime_get_ns() a l'entree,
   difference au retour)

## Lien avec la suite

Kprobes/kretprobes sont puissants mais lents (INT3). A partir de l'exercice
018, on passe a ftrace, le mecanisme que les vrais rootkits utilisent.
Ftrace patche les NOP en CALL, c'est plus rapide et plus discret.
