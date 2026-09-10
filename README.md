# EpiRootkit, projet SYS2 WLKOM

Cours EPITA APPING SYS2. Support de dev + docs pour le projet Wild Linux Kernel Object Module.

## Structure du depot

```
docs/    Cours, comprehension profonde, methodologie de recherche, grille d'eval
lab/     Env de dev kernel (QEMU + Debian jetable, ou virtme-ng pour Rootkit 101)
tps/     TPs incrementaux qui preparent au projet WLKOM
```

## Par ou commencer

Prof: lire docs/00-cours.md, provisionner le lab (lab/README.md), distribuer.

Etudiant: suivre lab/README.md pour installer, puis faire les TPs dans l'ordre.

## Prerequis

Machine Arch Linux (les etudiants EPITA en ont une). Connaissance C systeme (pointeurs, structures, syscalls). Bases GDB. 20 GB dispo (sources kernel + build).

## Environnement

Hote: Arch Linux avec GNOME.
Cible: kernel Linux dans QEMU (Debian minimal jetable) ou virtme-ng pour iteration rapide.
Kernel: 6.x sur Arch.

## Le projet WLKOM

Le projet final est WLKOM (voir given_files/epirootkit-subject.pdf). Ce depot fournit les TPs qui preparent aux features requises:

- Compile
- Connection persistante avec alerte visuelle
- Persistence apres reboot
- Executing commands avec retour stdout/stderr/exit
- Password
- Upload/download
- Hide (module list, ligne dans fichier, dir/files)
- Crypto

Les features sont notees dans le sujet WLKOM.

## Ressources externes

- TheXcellerator, serie 11 posts, reference ftrace hooking
- Diamorphine (m0nad), rootkit historique magic signals 31/63/64
- KoviD (carloslack), le plus complet actuellement, hooks filldir
- Elastic Security Labs "Hooked on Linux" (mars 2026), taxonomie
- LKRG, cote defensif
- linux-insides gitbook, kernel deep dive

## Licence

Materiel pedagogique EPITA APPING SYS2. Usage academique.
