# Deepdive 039: persistence via systemd

## Question ouverte

Un vrai rootkit ne veut pas qu'un admin voit un service suspect dans
`systemctl list-units`. Comment masquer un service systemd ?

Indices:
- systemd a un mecanisme de masquage: `systemctl mask`
- Les fichiers dans /run/systemd/ sont volatiles (perdus au reboot)
- Un rootkit pourrait renommer le .service pour ressembler a un service
  systeme legitime (ex: `dbus-org.freedesktop.systemd1.service`)

Cherche des exemples de rootkits reels qui abusent systemd.
Le rootkit Symbiote (2022) est un bon point de depart.

L'exercice 040 explore la persistence via initramfs, plus profonde et plus
difficile a detecter.
