# Deepdive 050: encrypted C2

## Felicitations

Tu as termine les 50 exercices rootkitlings. Tu as construit, etape par etape,
un rootkit kernel Linux avec:

- Modules kernel (LKM)
- Hooking (ftrace, kprobes)
- Hiding (module, fichiers, processus, ports)
- Privilege escalation (creds, magic signal)
- Persistence (modprobe, systemd, initramfs, self-reload)
- Communication (sockets kernel, protocole C2, execution de commandes)
- Chiffrement (XOR, AES-CBC)

## Pour le projet WLKOM

Tu as maintenant toutes les briques. Le projet WLKOM te demande de les
assembler dans un rootkit coherent avec un attacking program.

Ce qui reste a faire:
1. Combiner les techniques dans un seul module
2. Ecrire l'attacking program (userspace, en C ou Python)
3. Definir le protocole complet (authentication, commandes, upload/download)
4. Tester sur deux VMs (victime + attaquant)
5. Documenter EVERYTHING (comme le demande le sujet)

## Question finale

Comment un defenseur detecterait-il ton rootkit malgre toutes les techniques
de hiding ? Cherche:
- Volatility (forensics memoire)
- LKRG (Linux Kernel Runtime Guard)
- eBPF pour la detection en temps reel
- kprobes defensifs (ironie: la meme technique, utilisee par les deux cotes)

La securite offensive et defensive sont les deux faces de la meme piece.
Comprendre l'attaque est indispensable pour construire la defense.
