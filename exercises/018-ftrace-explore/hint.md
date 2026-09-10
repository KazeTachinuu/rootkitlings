# Hints, exercice 018

## Niveau 1

Les reponses viennent de commandes directes dans tracefs.
Assure-toi que tracefs est monte:
```bash
ls /sys/kernel/tracing/available_filter_functions
```

## Niveau 2

```bash
wc -l /sys/kernel/tracing/available_filter_functions
grep "sys_getdents64" /sys/kernel/tracing/available_filter_functions
grep "sys_kill" /sys/kernel/tracing/available_filter_functions
cat /sys/kernel/tracing/current_tracer
```

## Niveau 3

Reponses typiques (varient selon le kernel):
```
1. Nombre total: ~50000 (selon ton kernel)
2. __x64_sys_getdents64
3. __x64_sys_kill
4. nop
5. Vide ou "# tracer: nop" avec aucun evenement
```
