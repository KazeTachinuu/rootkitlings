# Deepdive, exercice 011

## KASLR

Les adresses dans /proc/kallsyms changent a chaque boot grace a KASLR
(Kernel Address Space Layout Randomization). Reboot la VM et compare
les adresses: elles sont differentes.

Questions:
1. Comment KASLR choisit-il le decalage? Cherche `kaslr_offset` dans
   arch/x86/boot/compressed/kaslr.c sur Elixir Bootlin.
2. Si un attaquant connait UNE adresse kernel, peut-il deduire les autres?
   (hint: oui, le decalage est le meme pour tous les symboles)
3. /proc/kallsyms expose TOUTES les adresses a root. C'est un probleme
   de securite. Cherche les discussions sur kptr_restrict et pourquoi
   certaines distros le mettent a 2 par defaut.

## Lien avec la suite

A l'exercice 012, tu vas apprendre a obtenir l'adresse de
kallsyms_lookup_name depuis un module kernel, malgre le fait qu'elle
n'est plus exportee depuis le kernel 5.7. Le trick utilise kprobes.
