# Deepdive, exercice 016

## Comment kprobes fonctionne sur x86

Quand tu enregistres un kprobe, le kernel:
1. Copie l'instruction originale au debut de la fonction
2. Remplace le premier octet par 0xCC (INT3, breakpoint)
3. Quand le CPU hit l'INT3, le trap handler appelle ton pre_handler
4. L'instruction originale est executee depuis la copie
5. Si tu as un post_handler, il est appele apres

Questions:
1. Que se passe-t-il si deux modules registrent un kprobe sur la meme
   fonction? (hint: cherche "aggr_kprobe" dans kernel/kprobes.c)
2. INT3 est une exception synchrone. C'est lent comparee a un appel
   de fonction normal. Mesure avec ktime_get_ns() dans ton handler:
   combien de nanosecondes ajoute le kprobe?
3. Ftrace (qu'on verra a l'exercice 019) utilise un mecanisme different:
   le compilateur insere des NOP au debut de chaque fonction, que ftrace
   peut patcher en CALL. C'est plus rapide qu'un INT3. Pourquoi utiliser
   kprobes alors? (hint: kprobes peut instrumenter n'importe quelle
   instruction, pas seulement le debut de fonction)

## Lien avec la suite

L'exercice 017 introduit kretprobes, qui interceptent le RETOUR d'une
fonction. Utile pour modifier la valeur de retour.
