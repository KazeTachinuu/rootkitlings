# rootkitlings

50 exercices progressifs pour comprendre les rootkits kernel Linux.
Chaque exercice explique le mecanisme kernel AVANT le code.
Tu ecris entre 1 et 10 lignes par exo. Zero etape sautee.

## Comment ca marche

1. Va dans le premier exercice: `cd exercises/001-hello-lkm`
2. Lis le README.md (le mecanisme, les liens Elixir Bootlin)
3. Ouvre src/*.c, complete les TODO(rootkitlings)
4. `make` pour compiler
5. Lance test.sh dans la VM pour verifier
6. Si bloque, lis hint.md (3 niveaux progressifs)
7. Passe au suivant

## Modes VM

- Exercices 001 a 015: `./lab/vng.sh` (boot 3s, suffisant pour printk et /proc)
- Exercices 016+: `./lab/vm.sh` (QEMU + Debian, isolation totale)

Le README de chaque exo indique quel mode utiliser.

## Progression

Phase 1 (001-010): LKM basics, ton premier module kernel
Phase 2 (011-015): Explorer le kernel, kallsyms, task_struct, creds
Phase 3 (016-025): Hooking, kprobes, ftrace, intercepter des fonctions
Phase 4 (026-033): Hiding, disparaitre de lsmod, ls, /proc, netstat
Phase 5 (034-037): Privilege escalation, devenir root depuis le kernel
Phase 6 (038-041): Persistence, survivre au reboot
Phase 7 (042-047): Communication, sockets kernel, C2, executer des commandes
Phase 8 (048-050): Crypto, chiffrer le trafic
