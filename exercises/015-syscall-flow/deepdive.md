# Deepdive, exercice 015

## SYSCALL_DEFINE et les wrappers

La macro SYSCALL_DEFINE genere deux couches:
- `__x64_sys_foo(struct pt_regs *regs)` : le wrapper qui extrait les arguments
  des registres (rdi, rsi, rdx, etc.)
- `__do_sys_foo(args...)` : la fonction avec les vrais arguments types

Questions:
1. Pourquoi cette indirection? Cherche "KPTI" et "Meltdown". Avant le fix,
   les arguments etaient passes directement. Apres, tout passe par pt_regs.
2. Ouvre la definition de SYSCALL_DEFINE3 dans include/linux/syscalls.h.
   Comment le 3 correspond-il au nombre d'arguments?
3. Si tu veux hooker un syscall, tu dois hooker le wrapper __x64_sys_foo,
   pas la fonction interne. Pourquoi? (hint: c'est le wrapper qui est dans
   sys_call_table)

## struct pt_regs

Quand tu hookeras des syscalls avec ftrace, tu recevras un `struct pt_regs *`.
Les arguments du syscall sont dans les registres:
- arg1 = regs->di
- arg2 = regs->si
- arg3 = regs->dx
- arg4 = regs->r10 (pas rcx, qui est ecrase par syscall)
- arg5 = regs->r8
- arg6 = regs->r9

Comprendre ca maintenant te fera gagner du temps aux exercices 019-024.

## Lien avec la suite

A partir de l'exercice 016, tu vas commencer a hooker des fonctions kernel.
Tu sais maintenant exactement quel chemin un syscall prend, et ou il est
possible de s'inserer pour intercepter.
