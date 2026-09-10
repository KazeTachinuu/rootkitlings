# Exercice 018: explorer ftrace et tracefs

## Le mecanisme

Ftrace (Function Tracer) est l'infrastructure de tracing du kernel Linux.
Le compilateur insere un appel a `__fentry__` (ou `mcount`) au tout debut
de chaque fonction kernel. Par defaut, ces appels sont patchees en NOP
(no-operation) par le kernel au boot. Quand ftrace est active pour une
fonction, le NOP est remplace par un CALL vers le framework ftrace.

C'est plus rapide que kprobes (pas de INT3/exception, juste un CALL direct).
C'est aussi le mecanisme que les rootkits modernes (Diamorphine, KoviD)
utilisent pour hooker des fonctions kernel.

L'interface userspace est dans tracefs, monte sur `/sys/kernel/tracing/`
(ou `/sys/kernel/debug/tracing/` sur les vieux kernels).

Fichiers importants dans tracefs:
- `available_filter_functions` : TOUTES les fonctions instrumentables par ftrace
- `current_tracer` : le tracer actif (nop = rien, function = trace les appels)
- `trace` : le buffer de trace
- `set_ftrace_filter` : filtrer quelles fonctions tracer
- `tracing_on` : 1 = trace active, 0 = desactivee

Code source:
- `kernel/trace/ftrace.c` : coeur de ftrace
  https://elixir.bootlin.com/linux/latest/source/kernel/trace/ftrace.c
- `kernel/trace/trace.c` : infrastructure de tracing
  https://elixir.bootlin.com/linux/latest/source/kernel/trace/trace.c

## Ce qu'il faut comprendre AVANT de chercher

1. Monte tracefs si pas deja fait:
   ```bash
   mount -t tracefs tracefs /sys/kernel/tracing 2>/dev/null || true
   ```

2. Regarde combien de fonctions sont instrumentables:
   ```bash
   wc -l /sys/kernel/tracing/available_filter_functions
   ```
   C'est typiquement entre 30 000 et 80 000 fonctions.

3. Cherche les fonctions syscall:
   ```bash
   grep "sys_getdents" /sys/kernel/tracing/available_filter_functions
   grep "sys_kill" /sys/kernel/tracing/available_filter_functions
   ```

4. Comprends que chaque fonction dans cette liste peut etre hookee par
   ftrace. C'est la surface d'attaque que les rootkits exploitent.

## L'exercice

Explore tracefs dans la VM et remplis `answers.txt`:

```
1. Nombre total de fonctions dans available_filter_functions: <nombre>
2. Nom exact de la fonction getdents64 dans la liste: <nom>
3. Nom exact de la fonction kill dans la liste: <nom>
4. Le tracer par defaut (cat current_tracer): <nom>
5. Que contient trace quand current_tracer=nop: <description courte>
```

Bonus: active le tracer "function" sur une seule fonction et regarde le resultat:
```bash
echo 0 > /sys/kernel/tracing/tracing_on
echo function > /sys/kernel/tracing/current_tracer
echo do_sys_openat2 > /sys/kernel/tracing/set_ftrace_filter
echo 1 > /sys/kernel/tracing/tracing_on
cat /etc/hostname
echo 0 > /sys/kernel/tracing/tracing_on
cat /sys/kernel/tracing/trace | head -20
echo nop > /sys/kernel/tracing/current_tracer
```

## Test

```bash
./test.sh
```
