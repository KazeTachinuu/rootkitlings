# Deepdive 045: protocole C2

## Question ouverte

Le protocole TLV qu'on utilise est trivial a detecter par un IDS (Intrusion
Detection System). Un analyste reseau verrait un paquet TCP avec un header
de 5 octets suivi de commandes shell en clair.

Comment les vrais C2 frameworks evitent-ils la detection ?

Pistes:
- Cobalt Strike utilise du HTTPS avec des profils "malleable C2" qui imitent
  du trafic web legitime (jQuery, Google Analytics)
- Sliver utilise mTLS, WireGuard, ou DNS tunneling
- Certains C2 communiquent via des services cloud (Slack, Dropbox, GitHub)

Cherche "malleable C2 profiles" pour comprendre comment on fait passer du
trafic C2 pour du trafic web normal.

L'exercice 046 ajoute l'execution de commandes via call_usermodehelper.
