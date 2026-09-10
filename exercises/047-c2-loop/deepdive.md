# Deepdive 047: boucle C2

## Question ouverte

Tu as un rootkit C2 fonctionnel. Mais il a plusieurs faiblesses:

1. La connexion est en clair (tout le trafic est lisible)
2. Le module n'est pas cache (lsmod le montre)
3. Il n'est pas persistent (un reboot le supprime)
4. L'IP du serveur est en dur dans le code

Pour le projet WLKOM, tu vas devoir combiner les techniques de:
- Hiding (exos 026-033) pour cacher le module
- Persistence (exos 038-041) pour survivre au reboot
- Crypto (exos 048-050) pour chiffrer le trafic

Comment structurerais-tu le code pour combiner tout ca proprement ?
Pense aux modules kernel qui s'appellent entre eux (inter-module communication).

Cherche EXPORT_SYMBOL et comment un module peut appeler les fonctions d'un autre.

Les exercices 048-050 ajoutent le chiffrement au C2.
